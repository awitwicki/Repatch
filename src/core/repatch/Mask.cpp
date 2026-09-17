#include "Mask.h"

#include <algorithm>
#include <cmath>

#include "FillBackend.h"
#include "Parallel.h"

namespace repatch {

void ThresholdMask( const float* mono, int w, int h, float threshold, uint8_t* out )
{
   const std::size_t n = std::size_t( w ) * h;
   for ( std::size_t i = 0; i < n; ++i )
      out[i] = ( mono[i] > threshold ) ? 1 : 0;
}

std::size_t CountSet( const Mask& m )
{
   std::size_t n = 0;
   for ( uint8_t v : m )
      n += ( v != 0 );
   return n;
}

BBox MaskBBox( const Mask& m, int w, int h )
{
   BBox b{ w, h, 0, 0 };
   for ( int y = 0; y < h; ++y )
   {
      const uint8_t* row = m.data() + std::size_t( y ) * w;
      for ( int x = 0; x < w; ++x )
         if ( row[x] )
         {
            b.x0 = std::min( b.x0, x );
            b.x1 = std::max( b.x1, x + 1 );
            b.y0 = std::min( b.y0, y );
            b.y1 = std::max( b.y1, y + 1 );
         }
   }
   if ( b.x1 <= b.x0 || b.y1 <= b.y0 )
      return BBox{};
   return b;
}

// Separable binary dilation using running counts: O(w*h) for any radius.
Mask DilateChebyshev( const Mask& m, int w, int h, int r )
{
   if ( r <= 0 )
      return m;
   Mask tmp( m.size(), 0 ), out( m.size(), 0 );
   std::vector<int> prefix;

   prefix.assign( std::size_t( w ) + 1, 0 );
   for ( int y = 0; y < h; ++y )
   {
      const uint8_t* row = m.data() + std::size_t( y ) * w;
      uint8_t* trow = tmp.data() + std::size_t( y ) * w;
      for ( int x = 0; x < w; ++x )
         prefix[x + 1] = prefix[x] + ( row[x] != 0 );
      for ( int x = 0; x < w; ++x )
      {
         int a = std::max( 0, x - r ), b = std::min( w, x + r + 1 );
         trow[x] = ( prefix[b] - prefix[a] ) > 0;
      }
   }

   prefix.assign( std::size_t( h ) + 1, 0 );
   for ( int x = 0; x < w; ++x )
   {
      for ( int y = 0; y < h; ++y )
         prefix[y + 1] = prefix[y] + ( tmp[std::size_t( y ) * w + x] != 0 );
      for ( int y = 0; y < h; ++y )
      {
         int a = std::max( 0, y - r ), b = std::min( h, y + r + 1 );
         out[std::size_t( y ) * w + x] = ( prefix[b] - prefix[a] ) > 0;
      }
   }
   return out;
}

namespace {

constexpr float kInf = 1e20f;

// Felzenszwalb & Huttenlocher one-dimensional squared distance transform.
// f: input (0 on set samples, kInf elsewhere), d: output, n: length,
// v/z: scratch of n and n+1 entries.
void EDT1D( const float* f, float* d, int n, int* v, float* z )
{
   int k = 0;
   v[0] = 0;
   z[0] = -kInf;
   z[1] = kInf;
   for ( int q = 1; q < n; ++q )
   {
      float s;
      for ( ;; )
      {
         s = ( ( f[q] + float( q ) * q ) - ( f[v[k]] + float( v[k] ) * v[k] ) ) / ( 2.0f * ( q - v[k] ) );
         if ( s <= z[k] && k > 0 )
            --k;
         else
            break;
      }
      ++k;
      v[k] = q;
      z[k] = s;
      z[k + 1] = kInf;
   }
   k = 0;
   for ( int q = 0; q < n; ++q )
   {
      while ( z[k + 1] < float( q ) )
         ++k;
      d[q] = float( q - v[k] ) * ( q - v[k] ) + f[v[k]];
   }
}

} // namespace

std::vector<float> DistanceTransform( const Mask& m, int w, int h, int threads )
{
   const std::size_t n = std::size_t( w ) * h;
   std::vector<float> g( n ), d( n );
   for ( std::size_t i = 0; i < n; ++i )
      g[i] = m[i] ? 0.0f : kInf;

   // columns
   ParallelBands( w, 0, threads, [&]( int x0, int x1, int ) {
      std::vector<float> f( h ), out( h ), z( std::size_t( h ) + 1 );
      std::vector<int> v( h );
      for ( int x = x0; x < x1; ++x )
      {
         for ( int y = 0; y < h; ++y )
            f[y] = g[std::size_t( y ) * w + x];
         EDT1D( f.data(), out.data(), h, v.data(), z.data() );
         for ( int y = 0; y < h; ++y )
            d[std::size_t( y ) * w + x] = out[y];
      }
   } );

   // rows
   ParallelBands( h, 0, threads, [&]( int y0, int y1, int ) {
      std::vector<float> out( w ), z( std::size_t( w ) + 1 );
      std::vector<int> v( w );
      for ( int y = y0; y < y1; ++y )
      {
         float* row = d.data() + std::size_t( y ) * w;
         EDT1D( row, out.data(), w, v.data(), z.data() );
         for ( int x = 0; x < w; ++x )
            row[x] = ( out[x] >= 1e19f ) ? 1e10f : std::sqrt( out[x] );
      }
   } );
   return d;
}

Mask ThresholdDistance( const std::vector<float>& dist, float radius )
{
   Mask out( dist.size(), 0 );
   for ( std::size_t i = 0; i < dist.size(); ++i )
      out[i] = ( dist[i] <= radius ) ? 1 : 0;
   return out;
}

Mask ValidSourceMap( const Mask& hole, int w, int h, int r,
                     const std::vector<float>& holeDist, int sampleRing )
{
   Mask touches = DilateChebyshev( hole, w, h, r ); // patch centred here overlaps the hole
   Mask valid( std::size_t( w ) * h, 0 );
   const float ring = ( sampleRing > 0 ) ? float( std::max( sampleRing, r + 1 ) ) : 0.0f;
   for ( int y = r; y < h - r; ++y )
      for ( int x = r; x < w - r; ++x )
      {
         std::size_t i = std::size_t( y ) * w + x;
         if ( touches[i] )
            continue;
         if ( sampleRing > 0 && holeDist[i] > ring )
            continue;
         valid[i] = 1;
      }
   return valid;
}

Mask CropMask( const Mask& m, int w, int h, const BBox& roi )
{
   (void)h;
   Mask out( std::size_t( roi.Width() ) * roi.Height(), 0 );
   for ( int y = 0; y < roi.Height(); ++y )
      std::copy( m.data() + std::size_t( roi.y0 + y ) * w + roi.x0,
                 m.data() + std::size_t( roi.y0 + y ) * w + roi.x1,
                 out.data() + std::size_t( y ) * roi.Width() );
   return out;
}

} // namespace repatch
