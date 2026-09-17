#include "Stroke.h"

#include <algorithm>
#include <cmath>

#include "PatchBackend.h"
#include "Random.h"

namespace repatch {

float BrushProfile( float d, int radius, float softness )
{
   const float r = float( std::max( 1, radius ) );
   if ( d > r )
      return 0.0f;
   const float s = std::min( 1.0f, std::max( 0.0f, softness ) );
   const float hard = r * ( 1.0f - s );
   if ( d <= hard )
      return 1.0f;
   const float t = ( d - hard ) / std::max( r - hard, 1e-6f ); // 0 at hard edge, 1 at radius
   const float u = 1.0f - t;
   return u * u * ( 3.0f - 2.0f * u );                          // smoothstep, 1 -> 0
}

BBox StrokeBounds( const Stroke& s, int w, int h )
{
   if ( s.points.empty() )
      return BBox{};
   const float r = float( std::max( 1, s.radius ) );
   float minX = s.points[0].x, maxX = minX, minY = s.points[0].y, maxY = minY;
   for ( const StrokePoint& p : s.points )
   {
      minX = std::min( minX, p.x ); maxX = std::max( maxX, p.x );
      minY = std::min( minY, p.y ); maxY = std::max( maxY, p.y );
   }
   BBox b;
   b.x0 = std::max( 0, int( std::floor( minX - r ) ) );
   b.y0 = std::max( 0, int( std::floor( minY - r ) ) );
   b.x1 = std::min( w, int( std::floor( maxX + r ) ) + 1 );
   b.y1 = std::min( h, int( std::floor( maxY + r ) ) + 1 );
   if ( b.Empty() )
      return BBox{};
   return b;
}

int EffectiveSampleRing( int sampleRing, int radius )
{
   return ( sampleRing > 0 ) ? sampleRing : std::max( 32, 3 * radius );
}

BBox StrokeRoi( const Stroke& s, int w, int h, const FillParams& params )
{
   BBox b = StrokeBounds( s, w, h );
   if ( b.Empty() )
      return b;
   const int r = params.patchSize / 2;
   const int ring = EffectiveSampleRing( params.sampleRing, s.radius );
   const int expand = params.feather + 2 * r + std::max( ring, params.searchRadius ) + 1;
   BBox roi;
   roi.x0 = std::max( 0, b.x0 - expand );
   roi.y0 = std::max( 0, b.y0 - expand );
   roi.x1 = std::min( w, b.x1 + expand );
   roi.y1 = std::min( h, b.y1 + expand );
   return roi;
}

void RasterizeStroke( const Stroke& s, const BBox& roi, Mask& mask, std::vector<float>& weight )
{
   const int W = roi.Width(), H = roi.Height();
   const std::size_t n = std::size_t( std::max( 0, W ) ) * std::max( 0, H );
   mask.assign( n, 0 );
   weight.assign( n, 0.0f );
   if ( n == 0 )
      return;
   const int r = std::max( 1, s.radius );
   const float opacity = std::min( 1.0f, std::max( 0.0f, s.opacity ) );
   std::vector<float> profile( n, 0.0f ); // opacity-independent, defines the hole
   for ( const StrokePoint& p : s.points )
   {
      const int cx = int( std::floor( p.x ) ), cy = int( std::floor( p.y ) );
      const int x0 = std::max( roi.x0, cx - r - 1 ), x1 = std::min( roi.x1 - 1, cx + r + 1 );
      const int y0 = std::max( roi.y0, cy - r - 1 ), y1 = std::min( roi.y1 - 1, cy + r + 1 );
      for ( int y = y0; y <= y1; ++y )
         for ( int x = x0; x <= x1; ++x )
         {
            const float dx = x + 0.5f - p.x, dy = y + 0.5f - p.y;
            const float v = BrushProfile( std::sqrt( dx * dx + dy * dy ), r, s.softness );
            if ( v > 0.0f )
            {
               std::size_t i = std::size_t( y - roi.y0 ) * W + ( x - roi.x0 );
               profile[i] = std::max( profile[i], v );
            }
         }
   }
   for ( std::size_t i = 0; i < n; ++i )
   {
      mask[i] = ( profile[i] > 0.0f ) ? 1 : 0;
      weight[i] = opacity * profile[i];
   }
}

uint32_t DeriveStrokeSeed( uint32_t seed, int strokeIndex )
{
   uint64_t h = HashSeed( seed, 0x7000u + uint32_t( strokeIndex ), 0u, 0u );
   uint32_t s = uint32_t( h );
   if ( s == 0 )
      s = uint32_t( h >> 32 );
   if ( s == 0 )
      s = 1;
   return s;
}

FillResult FillStroke( ImageView image, const Stroke& s, const FillParams& params, const ProgressFn& progress )
{
   FillResult res;
   if ( s.points.empty() )
   {
      res.error = "stroke has no points";
      return res;
   }
   if ( s.opacity <= 0.0f )
   {
      res.ok = true; // nothing would be composited
      return res;
   }
   FillParams p = params;
   p.sampleRing = EffectiveSampleRing( params.sampleRing, s.radius );
   // The brush's own softness already provides the soft edge (via the weight
   // map RasterizeStroke builds); running PatchBackend's separate feather ring
   // on top of it produces an uncoordinated double blend (a measured
   // discontinuity right at the brush edge) and makes brush Opacity have no
   // effect outside the hole, since the feather ring never consults `weight`.
   // Disabling feather here means synth == hole exactly for every stroke, so
   // there is no separate ring region left to conflict with the brush profile.
   p.feather = 0;
   const BBox roi = StrokeRoi( s, image.width, image.height, p );
   if ( roi.Empty() )
   {
      res.error = "stroke lies outside the image";
      return res;
   }
   const int W = roi.Width(), H = roi.Height();

   Image work( W, H, image.channels );
   for ( int c = 0; c < image.channels; ++c )
      for ( int y = 0; y < H; ++y )
         std::copy( image.plane[c] + std::size_t( roi.y0 + y ) * image.width + roi.x0,
                    image.plane[c] + std::size_t( roi.y0 + y ) * image.width + roi.x1,
                    work.Plane( c ) + std::size_t( y ) * W );

   Stroke local = s;
   for ( StrokePoint& q : local.points )
   {
      q.x -= float( roi.x0 );
      q.y -= float( roi.y0 );
   }
   Mask mask;
   std::vector<float> weight;
   RasterizeStroke( local, BBox{ 0, 0, W, H }, mask, weight );

   FillRequest req;
   req.image = work.View();
   req.mask = mask.data();
   req.weight = weight.data();
   req.params = p;
   PatchBackend backend;
   res = backend.Fill( req, progress );
   if ( !res.ok )
      return res;

   for ( int c = 0; c < image.channels; ++c )
      for ( int y = 0; y < H; ++y )
         std::copy( work.Plane( c ) + std::size_t( y ) * W,
                    work.Plane( c ) + std::size_t( y + 1 ) * W,
                    image.plane[c] + std::size_t( roi.y0 + y ) * image.width + roi.x0 );
   return res;
}

} // namespace repatch
