#include "testing.h"
#include "repatch/Diffusion.h"

TEST_CASE( diffusion_fills_hole_in_linear_ramp_exactly )
{
   const int w = 40, h = 30;
   repatch::Image img( w, h, 2 );
   repatch::Mask hole( size_t( w ) * h, 0 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         img.At( 0, x, y ) = float( x ) / ( w - 1 );
         img.At( 1, x, y ) = 0.5f * float( y ) / ( h - 1 ) + 0.1f;
         if ( x >= 12 && x < 24 && y >= 8 && y < 20 )
         {
            hole[size_t( y ) * w + x] = 1;
            img.At( 0, x, y ) = 7.0f; // garbage inside the hole
            img.At( 1, x, y ) = -3.0f;
         }
      }
   int sweeps = repatch::DiffusionFill( img, hole, 3 );
   CHECK( sweeps > 0 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         CHECK_NEAR( img.At( 0, x, y ), float( x ) / ( w - 1 ), 1e-3 );
         CHECK_NEAR( img.At( 1, x, y ), 0.5f * float( y ) / ( h - 1 ) + 0.1f, 1e-3 );
      }
}

TEST_CASE( diffusion_leaves_known_pixels_untouched_and_is_deterministic )
{
   // The hole's bounding-box height (40 rows: y in [6,45]) exceeds
   // kBandHeight (16), so ParallelBands genuinely splits this hole across
   // multiple bands (BandCount(40, 0) = (40+15)/16 = 3) -- this makes the
   // threads=1 vs threads=5 comparison below actually exercise concurrent
   // band execution, not just two serial runs of a single band.
   const int w = 50, h = 50;
   repatch::Image a( w, h, 1 );
   repatch::Mask hole( size_t( w ) * h, 0 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         a.At( 0, x, y ) = float( ( x * 7 + y * 13 ) % 17 ) / 17.0f;
         if ( x > 10 && x < 40 && y > 5 && y < 46 )
            hole[size_t( y ) * w + x] = 1;
      }
   repatch::Image b = a;
   repatch::DiffusionFill( a, hole, 1 );
   repatch::DiffusionFill( b, hole, 5 );
   CHECK( a.data == b.data );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
         if ( !hole[size_t( y ) * w + x] )
            CHECK( a.At( 0, x, y ) == float( ( x * 7 + y * 13 ) % 17 ) / 17.0f );
}

TEST_CASE( diffusion_empty_hole_is_noop )
{
   repatch::Image img( 8, 8, 1 );
   repatch::Mask hole( 64, 0 );
   CHECK( repatch::DiffusionFill( img, hole, 2 ) == 0 );
}
