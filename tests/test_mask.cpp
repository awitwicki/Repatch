#include "testing.h"
#include "repatch/FillBackend.h"
#include "repatch/Mask.h"
#include "repatch/Random.h"

#include <cmath>
#include <limits>

TEST_CASE( mask_threshold_uses_strict_greater )
{
   const float in[6] = { 0.0f, 0.5f, 0.5001f, 1.0f, -1.0f, 0.49f };
   uint8_t out[6];
   repatch::ThresholdMask( in, 3, 2, 0.5f, out );
   CHECK( out[0] == 0 );
   CHECK( out[1] == 0 ); // equal to threshold is NOT a hole
   CHECK( out[2] == 1 );
   CHECK( out[3] == 1 );
   CHECK( out[4] == 0 );
   CHECK( out[5] == 0 );
}

static repatch::Mask RectMask( int w, int h, int x0, int y0, int x1, int y1 )
{
   repatch::Mask m( size_t( w ) * h, 0 );
   for ( int y = y0; y < y1; ++y )
      for ( int x = x0; x < x1; ++x )
         m[size_t( y ) * w + x] = 1;
   return m;
}

TEST_CASE( mask_bbox_and_count )
{
   repatch::Mask m = RectMask( 20, 10, 3, 2, 8, 6 );
   CHECK( repatch::CountSet( m ) == 5 * 4 );
   repatch::BBox b = repatch::MaskBBox( m, 20, 10 );
   CHECK( b.x0 == 3 && b.y0 == 2 && b.x1 == 8 && b.y1 == 6 );
   CHECK( b.Width() == 5 && b.Height() == 4 );
   repatch::Mask empty( 200, 0 );
   CHECK( repatch::MaskBBox( empty, 20, 10 ).Empty() );
}

static repatch::Mask BruteDilate( const repatch::Mask& m, int w, int h, int r )
{
   repatch::Mask out( m.size(), 0 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         bool any = false;
         for ( int dy = -r; dy <= r && !any; ++dy )
            for ( int dx = -r; dx <= r && !any; ++dx )
            {
               int xx = x + dx, yy = y + dy;
               if ( xx >= 0 && yy >= 0 && xx < w && yy < h && m[size_t( yy ) * w + xx] )
                  any = true;
            }
         out[size_t( y ) * w + x] = any;
      }
   return out;
}

TEST_CASE( mask_dilate_chebyshev_matches_brute_force )
{
   const int w = 37, h = 23;
   repatch::Pcg32 rng( 3 );
   repatch::Mask m( size_t( w ) * h, 0 );
   for ( auto& v : m ) v = ( rng.Uniform() < 0.05f );
   for ( int r : { 0, 1, 2, 5, 40 } )
      CHECK( repatch::DilateChebyshev( m, w, h, r ) == BruteDilate( m, w, h, r ) );
}

TEST_CASE( mask_distance_transform_matches_brute_force )
{
   const int w = 31, h = 19;
   repatch::Pcg32 rng( 11 );
   repatch::Mask m( size_t( w ) * h, 0 );
   for ( auto& v : m ) v = ( rng.Uniform() < 0.03f );
   m[5 * w + 7] = 1; // ensure non-empty
   std::vector<float> d = repatch::DistanceTransform( m, w, h, 3 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         float best = std::numeric_limits<float>::max();
         for ( int yy = 0; yy < h; ++yy )
            for ( int xx = 0; xx < w; ++xx )
               if ( m[size_t( yy ) * w + xx] )
                  best = std::min( best, std::sqrt( float( ( xx - x ) * ( xx - x ) + ( yy - y ) * ( yy - y ) ) ) );
         CHECK_NEAR( d[size_t( y ) * w + x], best, 1e-3 );
      }
}

TEST_CASE( mask_distance_transform_empty_mask_is_far )
{
   repatch::Mask m( 12 * 9, 0 );
   std::vector<float> d = repatch::DistanceTransform( m, 12, 9, 2 );
   for ( float v : d )
      CHECK( v >= 1e9f );
}

TEST_CASE( mask_threshold_distance )
{
   std::vector<float> d = { 0.f, 1.f, 2.5f, 3.f, 3.01f };
   repatch::Mask m = repatch::ThresholdDistance( d, 3.0f );
   CHECK( m[0] == 1 && m[1] == 1 && m[2] == 1 && m[3] == 1 && m[4] == 0 );
}

TEST_CASE( mask_valid_source_map_never_overlaps_hole_and_respects_ring )
{
   const int w = 64, h = 48, r = 3;
   repatch::Mask hole = RectMask( w, h, 20, 15, 30, 25 );
   std::vector<float> dist = repatch::DistanceTransform( hole, w, h, 2 );

   for ( int ring : { 0, 2, 10 } )
   {
      repatch::Mask valid = repatch::ValidSourceMap( hole, w, h, r, dist, ring );
      CHECK( repatch::CountSet( valid ) > 0 );
      for ( int y = 0; y < h; ++y )
         for ( int x = 0; x < w; ++x )
         {
            if ( !valid[size_t( y ) * w + x] )
               continue;
            CHECK( x >= r && x < w - r && y >= r && y < h - r );
            for ( int dy = -r; dy <= r; ++dy )
               for ( int dx = -r; dx <= r; ++dx )
                  CHECK( hole[size_t( y + dy ) * w + ( x + dx )] == 0 );
            if ( ring > 0 )
               CHECK( dist[size_t( y ) * w + x] <= float( std::max( ring, r + 1 ) ) + 1e-4f );
         }
   }
   // ring smaller than r+1 is floored to r+1 and still yields sources
   repatch::Mask tight = repatch::ValidSourceMap( hole, w, h, r, dist, 1 );
   CHECK( repatch::CountSet( tight ) > 0 );
}

TEST_CASE( mask_valid_source_map_empty_when_nothing_fits )
{
   const int w = 20, h = 20, r = 5;
   repatch::Mask hole = RectMask( w, h, 5, 5, 15, 15 );
   std::vector<float> dist = repatch::DistanceTransform( hole, w, h, 1 );
   repatch::Mask valid = repatch::ValidSourceMap( hole, w, h, r, dist, 0 );
   CHECK( repatch::CountSet( valid ) == 0 );
}

TEST_CASE( mask_crop )
{
   repatch::Mask m = RectMask( 10, 10, 2, 3, 6, 7 );
   repatch::BBox roi{ 1, 2, 7, 8 };
   repatch::Mask c = repatch::CropMask( m, 10, 10, roi );
   REQUIRE( c.size() == 36 );
   CHECK( repatch::CountSet( c ) == 16 );
   CHECK( c[size_t( 1 ) * 6 + 1] == 1 ); // (x=2,y=3) -> (1,1)
   CHECK( c[0] == 0 );
}
