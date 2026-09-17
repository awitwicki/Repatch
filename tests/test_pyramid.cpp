#include "testing.h"
#include "repatch/Pyramid.h"

TEST_CASE( pyramid_half_size_rounds_up )
{
   CHECK( repatch::HalfSize( 10 ) == 5 );
   CHECK( repatch::HalfSize( 11 ) == 6 );
   CHECK( repatch::HalfSize( 1 ) == 1 );
}

TEST_CASE( pyramid_downsample_image_sizes_and_constant )
{
   repatch::Image img( 11, 7, 3 );
   for ( int c = 0; c < 3; ++c )
      for ( int y = 0; y < 7; ++y )
         for ( int x = 0; x < 11; ++x )
            img.At( c, x, y ) = 0.25f * ( c + 1 );
   repatch::Image d = repatch::DownsampleImage( img, 2 );
   CHECK( d.width == 6 && d.height == 4 && d.channels == 3 );
   for ( int c = 0; c < 3; ++c )
      for ( int y = 0; y < d.height; ++y )
         for ( int x = 0; x < d.width; ++x )
            CHECK_NEAR( d.At( c, x, y ), 0.25f * ( c + 1 ), 1e-6 );
}

TEST_CASE( pyramid_downsample_image_is_binomial )
{
   // Coarse pixel X covers fine pixels 2X-1..2X+1 with weights 1/4, 1/2, 1/4.
   // A single bright fine pixel at (4,4) lies only in the support of coarse
   // (2,2) (2X = 4, centre weight 1/2 per axis) -> value 1/4 there, 0 elsewhere.
   repatch::Image img( 9, 9, 1 );
   img.At( 0, 4, 4 ) = 1.0f;
   repatch::Image d = repatch::DownsampleImage( img, 1 );
   CHECK_NEAR( d.At( 0, 2, 2 ), 0.25, 1e-6 );
   CHECK_NEAR( d.At( 0, 1, 2 ), 0.0, 1e-6 );
   CHECK_NEAR( d.At( 0, 3, 2 ), 0.0, 1e-6 );
   CHECK_NEAR( d.At( 0, 2, 3 ), 0.0, 1e-6 );
   // A bright fine pixel at (5,5) is in the support of coarse 2 (3..5) and 3 (5..7)
   // on each axis, with edge weight 1/4 -> four coarse pixels get 1/16.
   repatch::Image img2( 9, 9, 1 );
   img2.At( 0, 5, 5 ) = 1.0f;
   repatch::Image d2 = repatch::DownsampleImage( img2, 1 );
   CHECK_NEAR( d2.At( 0, 2, 2 ), 0.0625, 1e-6 );
   CHECK_NEAR( d2.At( 0, 3, 2 ), 0.0625, 1e-6 );
   CHECK_NEAR( d2.At( 0, 2, 3 ), 0.0625, 1e-6 );
   CHECK_NEAR( d2.At( 0, 3, 3 ), 0.0625, 1e-6 );
}

TEST_CASE( pyramid_downsample_mask_is_conservative )
{
   // One hole pixel at fine (6,6). Coarse pixels whose 3x3 support (2X-1..2X+1)
   // contains it: X in {3} only (support 5..7). So exactly coarse (3,3) is set.
   repatch::Mask m( 12 * 12, 0 );
   m[6 * 12 + 6] = 1;
   repatch::Mask d = repatch::DownsampleMask( m, 12, 12 );
   REQUIRE( d.size() == 36 );
   int count = 0;
   for ( auto v : d ) count += v;
   CHECK( count == 1 );
   CHECK( d[3 * 6 + 3] == 1 );

   // hole at fine (5,5): supports of X=2 (3..5) and X=3 (5..7) both contain 5 -> 4 coarse pixels set
   repatch::Mask m2( 12 * 12, 0 );
   m2[5 * 12 + 5] = 1;
   repatch::Mask d2 = repatch::DownsampleMask( m2, 12, 12 );
   count = 0;
   for ( auto v : d2 ) count += v;
   CHECK( count == 4 );
   CHECK( d2[2 * 6 + 2] && d2[2 * 6 + 3] && d2[3 * 6 + 2] && d2[3 * 6 + 3] );
}

TEST_CASE( pyramid_auto_levels_formula )
{
   repatch::BBox b88{ 0, 0, 88, 88 };
   CHECK( repatch::AutoPyramidLevels( b88, 11 ) == 4 ); // 1 + floor(log2(8))
   repatch::BBox b10{ 0, 0, 10, 10 };
   CHECK( repatch::AutoPyramidLevels( b10, 11 ) == 1 );
   repatch::BBox bBig{ 0, 0, 10000, 10000 };
   CHECK( repatch::AutoPyramidLevels( bBig, 5 ) == 8 );
   repatch::BBox bRect{ 0, 0, 200, 30 };
   CHECK( repatch::AutoPyramidLevels( bRect, 11 ) == 2 ); // min side 30/11 = 2.7 -> 1+1
}

TEST_CASE( pyramid_cap_levels_by_image_size )
{
   // 64x64, patch 11: level sizes 64,32,16,... coarsest must be >= 22 -> 2 levels
   CHECK( repatch::CapPyramidLevels( 8, 64, 64, 11 ) == 2 );
   CHECK( repatch::CapPyramidLevels( 1, 8, 8, 11 ) == 1 );  // never below 1
   CHECK( repatch::CapPyramidLevels( 3, 1000, 1000, 11 ) == 3 );
   CHECK( repatch::CapPyramidLevels( 8, 1000, 40, 11 ) == 1 ); // 40 -> 20 < 22
}
