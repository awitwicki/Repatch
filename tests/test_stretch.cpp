#include "testing.h"
#include "repatch/Stretch.h"
#include "repatch/Random.h"

#include <algorithm>
#include <limits>
#include <vector>

TEST_CASE( stretch_mtf_identity_and_clipping )
{
   CHECK_NEAR( repatch::MTF( 0.5f, 0.3f ), 0.3f, 1e-6 );
   CHECK_NEAR( repatch::MTF( 0.25f, 0.0f ), 0.0f, 1e-6 );
   CHECK_NEAR( repatch::MTF( 0.25f, 1.0f ), 1.0f, 1e-6 );
   CHECK_NEAR( repatch::MTF( 0.25f, -0.5f ), 0.0f, 1e-6 );
   CHECK_NEAR( repatch::MTF( 0.25f, 2.0f ), 1.0f, 1e-6 );
   // MTF(m, x) = y  <=>  MTF(y, x) = m
   float y = repatch::MTF( 0.25f, 0.5f );
   CHECK_NEAR( y, 0.75f, 1e-6 );
   CHECK_NEAR( repatch::MTF( y, 0.5f ), 0.25f, 1e-6 );
}

TEST_CASE( stretch_is_monotone )
{
   repatch::StretchParams p{ 0.1f, 0.05f };
   float prev = -1;
   for ( int i = 0; i <= 1000; ++i )
   {
      float x = i / 1000.0f;
      float s = repatch::ApplyStretch( p, x );
      CHECK( s >= prev );
      CHECK( s >= 0.0f && s <= 1.0f );
      prev = s;
   }
}

TEST_CASE( stretch_auto_maps_median_to_quarter )
{
   // Linear-like data: background 1e-3 with 2e-4 noise, plus a few bright pixels.
   const int w = 200, h = 200;
   std::vector<float> plane( size_t( w ) * h );
   repatch::Pcg32 rng( 5 );
   for ( auto& v : plane ) v = 1e-3f + 2e-4f * ( rng.Uniform() - 0.5f );
   for ( int i = 0; i < 50; ++i ) plane[rng.Below( plane.size() )] = 0.9f;

   repatch::StretchParams p = repatch::ComputeAutoStretch( plane.data(), w, h, repatch::Mask() );
   std::vector<float> sorted = plane;
   std::nth_element( sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end() );
   float median = sorted[sorted.size() / 2];
   CHECK( p.c0 < median );
   CHECK( p.c0 >= 0.0f );
   CHECK_NEAR( repatch::ApplyStretch( p, median ), 0.25f, 0.01 );
   CHECK( repatch::ApplyStretch( p, 0.9f ) > 0.9f );
}

TEST_CASE( stretch_auto_ignores_excluded_pixels )
{
   const int w = 50, h = 50;
   std::vector<float> plane( size_t( w ) * h, 0.2f );
   repatch::Mask exclude( plane.size(), 0 );
   for ( int i = 0; i < 1000; ++i ) { plane[i] = 0.9f; exclude[i] = 1; }
   repatch::StretchParams withEx = repatch::ComputeAutoStretch( plane.data(), w, h, exclude );
   // All known pixels are 0.2 -> MAD = 0 -> c0 = 0; median 0.2 maps to 0.25
   CHECK_NEAR( repatch::ApplyStretch( withEx, 0.2f ), 0.25f, 1e-4 );
}

TEST_CASE( stretch_auto_degenerate_is_identity )
{
   std::vector<float> zeros( 100, 0.0f );
   repatch::StretchParams p = repatch::ComputeAutoStretch( zeros.data(), 10, 10, repatch::Mask() );
   CHECK_NEAR( p.c0, 0.0f, 1e-9 );
   CHECK_NEAR( p.m, 0.5f, 1e-9 );
}

TEST_CASE( is_finite_sample_matches_ieee754_semantics )
{
   // Bit-pattern-based check: must agree with real finiteness regardless of
   // -ffinite-math-only, which this test suite is NOT built with, but which
   // the PixInsight module build is (see Fix 2 / developer-guide.md).
   float nan = std::numeric_limits<float>::quiet_NaN();
   float inf = std::numeric_limits<float>::infinity();
   CHECK( !repatch::IsFiniteSample( nan ) );
   CHECK( !repatch::IsFiniteSample( inf ) );
   CHECK( !repatch::IsFiniteSample( -inf ) );
   CHECK( repatch::IsFiniteSample( 0.0f ) );
   CHECK( repatch::IsFiniteSample( 1.5f ) );
   CHECK( repatch::IsFiniteSample( -1.5f ) );
}
