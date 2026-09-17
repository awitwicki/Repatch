#include "testing.h"
#include "repatch/Stroke.h"
#include "repatch/Random.h"

#include <cmath>
#include <vector>

TEST_CASE( stroke_profile_hard_and_soft )
{
   CHECK_NEAR( repatch::BrushProfile( 0.0f, 10, 0.0f ), 1.0, 1e-6 );
   CHECK_NEAR( repatch::BrushProfile( 10.0f, 10, 0.0f ), 1.0, 1e-6 );
   CHECK_NEAR( repatch::BrushProfile( 10.01f, 10, 0.0f ), 0.0, 1e-6 );
   CHECK_NEAR( repatch::BrushProfile( 5.0f, 10, 0.5f ), 1.0, 1e-6 );
   float mid = repatch::BrushProfile( 7.5f, 10, 0.5f );
   CHECK( mid > 0.4f && mid < 0.6f );
   CHECK_NEAR( repatch::BrushProfile( 10.0f, 10, 0.5f ), 0.0, 1e-6 );
   CHECK_NEAR( repatch::BrushProfile( 0.0f, 10, 1.0f ), 1.0, 1e-6 );
   CHECK_NEAR( repatch::BrushProfile( 5.0f, 10, 1.0f ), 0.5, 1e-6 );
   float prev = 2.0f;
   for ( int i = 0; i <= 200; ++i )
   {
      float d = 12.0f * i / 200.0f;
      float v = repatch::BrushProfile( d, 10, 0.7f );
      CHECK( v <= prev + 1e-7f );
      CHECK( v >= 0.0f && v <= 1.0f );
      prev = v;
   }
}

TEST_CASE( stroke_bounds_clipping )
{
   repatch::Stroke s;
   s.radius = 3;
   s.points = { { 5.5f, 5.5f } };
   repatch::BBox b = repatch::StrokeBounds( s, 20, 20 );
   CHECK( b.x0 == 2 && b.y0 == 2 && b.x1 == 9 && b.y1 == 9 );

   s.points = { { 1.0f, 1.0f } };
   s.radius = 5;
   b = repatch::StrokeBounds( s, 20, 20 );
   CHECK( b.x0 == 0 && b.y0 == 0 && b.x1 == 7 && b.y1 == 7 );

   s.points = { { -100.0f, -100.0f } };
   s.radius = 3;
   CHECK( repatch::StrokeBounds( s, 20, 20 ).Empty() );

   s.points = { { 2.0f, 2.0f }, { 15.0f, 10.0f } };
   s.radius = 2;
   b = repatch::StrokeBounds( s, 20, 20 );
   CHECK( b.x0 == 0 && b.y0 == 0 && b.x1 == 18 && b.y1 == 13 );

   s.points.clear();
   CHECK( repatch::StrokeBounds( s, 20, 20 ).Empty() );
}

TEST_CASE( stroke_effective_ring_and_roi )
{
   CHECK( repatch::EffectiveSampleRing( 0, 15 ) == 45 );
   CHECK( repatch::EffectiveSampleRing( 0, 5 ) == 32 );
   CHECK( repatch::EffectiveSampleRing( 20, 15 ) == 20 );

   repatch::Stroke s;
   s.radius = 4;
   s.points = { { 50.0f, 50.0f } };
   repatch::FillParams p;
   p.patchSize = 11; p.feather = 3; p.sampleRing = 0; p.searchRadius = 0;
   // bounds [46,55) grown by 3 + 10 + max(32, 12) + 1 = 46
   repatch::BBox roi = repatch::StrokeRoi( s, 200, 200, p );
   CHECK( roi.x0 == 0 && roi.y0 == 0 && roi.x1 == 101 && roi.y1 == 101 );
   p.sampleRing = 10; p.searchRadius = 20;
   roi = repatch::StrokeRoi( s, 200, 200, p ); // grown by 3 + 10 + 20 + 1 = 34
   CHECK( roi.x0 == 12 && roi.x1 == 89 );
}

static float BruteWeight( const repatch::Stroke& s, int x, int y )
{
   float best = 0;
   for ( const repatch::StrokePoint& p : s.points )
   {
      float d = std::sqrt( ( x + 0.5f - p.x ) * ( x + 0.5f - p.x ) + ( y + 0.5f - p.y ) * ( y + 0.5f - p.y ) );
      best = std::max( best, repatch::BrushProfile( d, s.radius, s.softness ) );
   }
   return best;
}

TEST_CASE( stroke_rasterize_matches_brute_force )
{
   repatch::Stroke s;
   s.radius = 6; s.softness = 0.4f; s.opacity = 0.8f;
   repatch::Pcg32 rng( 3 );
   float x = 20, y = 20;
   for ( int i = 0; i < 40; ++i )
   {
      s.points.push_back( { x, y } );
      x += 1.5f * rng.Uniform();
      y += 1.5f * rng.Uniform() - 0.3f;
   }
   const int w = 120, h = 80;
   repatch::BBox roi = repatch::StrokeBounds( s, w, h );
   repatch::Mask mask;
   std::vector<float> weight;
   repatch::RasterizeStroke( s, roi, mask, weight );
   REQUIRE( mask.size() == size_t( roi.Width() ) * roi.Height() );
   for ( int yy = roi.y0; yy < roi.y1; ++yy )
      for ( int xx = roi.x0; xx < roi.x1; ++xx )
      {
         size_t i = size_t( yy - roi.y0 ) * roi.Width() + ( xx - roi.x0 );
         float bw = BruteWeight( s, xx, yy );
         CHECK_NEAR( weight[i], 0.8f * bw, 1e-5 );
         CHECK( mask[i] == ( bw > 0.0f ? 1 : 0 ) );
         CHECK( weight[i] >= 0.0f && weight[i] <= 1.0f );
      }
}

TEST_CASE( stroke_rasterize_opacity_scales_weight_not_mask )
{
   repatch::Stroke s;
   s.radius = 5; s.softness = 0.0f; s.opacity = 0.25f;
   s.points = { { 10.5f, 10.5f } };
   repatch::BBox roi{ 0, 0, 21, 21 };
   repatch::Mask mask; std::vector<float> weight;
   repatch::RasterizeStroke( s, roi, mask, weight );
   CHECK_NEAR( weight[10 * 21 + 10], 0.25, 1e-6 );
   CHECK( mask[10 * 21 + 10] == 1 );
   CHECK( repatch::CountSet( mask ) > 60 && repatch::CountSet( mask ) < 100 ); // ~pi*25
}

TEST_CASE( stroke_seed_derivation )
{
   uint32_t a = repatch::DeriveStrokeSeed( 7, 0 );
   uint32_t b = repatch::DeriveStrokeSeed( 7, 1 );
   uint32_t c = repatch::DeriveStrokeSeed( 8, 0 );
   CHECK( a != 0 && b != 0 && c != 0 );
   CHECK( a != b && a != c );
   CHECK( repatch::DeriveStrokeSeed( 7, 0 ) == a );
   for ( int i = 0; i < 1000; ++i )
      CHECK( repatch::DeriveStrokeSeed( 12345, i ) != 0 );
}

static repatch::Image SineImage( int w, int h )
{
   repatch::Image img( w, h, 1 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
         img.At( 0, x, y ) = 0.5f + 0.25f * std::sin( 2 * 3.14159265f * x / 16 ) + 0.2f * std::sin( 2 * 3.14159265f * y / 16 );
   return img;
}

static std::vector<repatch::Stroke> ThreeStrokes()
{
   std::vector<repatch::Stroke> v( 3 );
   for ( int k = 0; k < 3; ++k )
   {
      v[k].radius = 8; v[k].softness = 0.5f; v[k].opacity = 1.0f;
      for ( int i = 0; i <= 12; ++i )
         v[k].points.push_back( { 40.0f + 4.0f * i + 20.0f * k, 30.0f + 3.0f * i + 10.0f * k } );
   }
   return v;
}

static void Replay( repatch::Image& img, int threads )
{
   repatch::FillParams p;
   p.patchSize = 9; p.iterations = 4; p.feather = 2; p.threads = threads;
   p.matchSpace = repatch::MatchSpace::Linear;
   std::vector<repatch::Stroke> strokes = ThreeStrokes();
   for ( int k = 0; k < 3; ++k )
   {
      p.randomSeed = repatch::DeriveStrokeSeed( 5, k );
      repatch::FillResult r = repatch::FillStroke( img.View(), strokes[k], p );
      REQUIRE( r.ok );
   }
}

TEST_CASE( stroke_fill_replays_deterministically )
{
   repatch::Image a = SineImage( 160, 120 );
   repatch::Image b = SineImage( 160, 120 );
   repatch::Image original = a;
   Replay( a, 1 );
   Replay( b, 7 );
   CHECK( a.data == b.data );
   // something changed under the strokes, nothing far away from them
   int changed = 0;
   for ( size_t i = 0; i < a.data.size(); ++i )
      changed += ( a.data[i] != original.data[i] );
   CHECK( changed > 100 );
   CHECK( a.At( 0, 2, 2 ) == original.At( 0, 2, 2 ) );
   CHECK( a.At( 0, 157, 117 ) == original.At( 0, 157, 117 ) );
}

TEST_CASE( stroke_fill_opacity_zero_is_noop_and_outside_errors )
{
   repatch::Image img = SineImage( 64, 64 );
   repatch::Image original = img;
   repatch::Stroke s = ThreeStrokes()[0];
   s.opacity = 0.0f;
   repatch::FillParams p;
   p.patchSize = 7; p.randomSeed = 3; p.matchSpace = repatch::MatchSpace::Linear;
   repatch::FillResult r = repatch::FillStroke( img.View(), s, p );
   CHECK( r.ok );
   CHECK( img.data == original.data );

   repatch::Stroke out;
   out.radius = 5;
   out.points = { { -50.0f, -50.0f } };
   r = repatch::FillStroke( img.View(), out, p );
   CHECK( !r.ok );
   CHECK( !r.error.empty() );

   repatch::Stroke none;
   r = repatch::FillStroke( img.View(), none, p );
   CHECK( !r.ok );
}

// Sine texture plus a little per-pixel noise, so no patch elsewhere in the
// image is a bit-exact match for one under the stroke (a pure sine period
// would let PatchMatch reproduce the original exactly, hiding an opacity
// effect that would otherwise be there).
static repatch::Image TexturedImage( int w, int h, uint32_t seed )
{
   repatch::Image img( w, h, 1 );
   repatch::Pcg32 rng( seed );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         float v = 0.5f + 0.25f * std::sin( 2 * 3.14159265f * x / 16 ) + 0.2f * std::sin( 2 * 3.14159265f * y / 16 );
         v += 0.05f * ( rng.Uniform() - 0.5f );
         img.At( 0, x, y ) = v;
      }
   return img;
}

// Regression test for the feather/weight double-blend: FillStroke must
// force params.feather = 0 internally, so PatchBackend's separate
// feather-ring blend never runs on top of the brush's own weight map. Proof:
// (1) with a large, non-default feather still passed in, pixels the brush
// profile never reaches (strictly outside the stroke's disc, including its
// soft shoulder) come back bit-identical to the source image -- if the old
// feather ring were still active it would touch a band of exactly those
// pixels just outside the disc; (2) Opacity, which only ever influences the
// hole's own compositing weight, visibly changes pixels at the soft edge
// (0 < BrushProfile < 1), proving the hole is composited through the weight
// map rather than being a fixed full replacement.
TEST_CASE( stroke_fill_feather_forced_off_and_opacity_controls_edge )
{
   const int w = 96, h = 96;
   repatch::Image original = TexturedImage( w, h, 99 );

   repatch::Stroke s;
   s.radius = 12;
   s.softness = 0.5f;
   s.points = { { 48.0f, 48.0f } };

   auto fillWithOpacity = [&]( float opacity ) -> repatch::Image {
      repatch::Image img = original;
      repatch::Stroke local = s;
      local.opacity = opacity;
      repatch::FillParams p;
      p.patchSize = 9;
      p.iterations = 4;
      // Deliberately non-default and non-zero: if FillStroke did not force
      // this to 0 internally, PatchBackend would blend a feather ring just
      // outside the brush's own soft edge, independently of `weight`.
      p.feather = 5;
      p.matchSpace = repatch::MatchSpace::Linear;
      p.randomSeed = 42;
      repatch::FillResult r = repatch::FillStroke( img.View(), local, p );
      // CHECK, not REQUIRE: REQUIRE's bare `return` doesn't match this
      // lambda's non-void return type. On failure `img` is still the
      // untouched original (FillBackend never modifies on error), so it's
      // safe to keep going and let the CHECK below report the failure.
      CHECK( r.ok );
      return img;
   };

   repatch::Image full = fillWithOpacity( 1.0f );
   repatch::Image half = fillWithOpacity( 0.3f );

   int outsideChecked = 0, edgeChecked = 0;
   bool edgeDiffers = false;
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         float dx = x + 0.5f - s.points[0].x, dy = y + 0.5f - s.points[0].y;
         float d = std::sqrt( dx * dx + dy * dy );
         float profile = repatch::BrushProfile( d, s.radius, s.softness );
         if ( profile == 0.0f )
         {
            ++outsideChecked;
            CHECK( full.At( 0, x, y ) == original.At( 0, x, y ) );
            CHECK( half.At( 0, x, y ) == original.At( 0, x, y ) );
         }
         else if ( profile > 0.0f && profile < 1.0f )
         {
            ++edgeChecked;
            if ( full.At( 0, x, y ) != half.At( 0, x, y ) )
               edgeDiffers = true;
         }
      }
   CHECK( outsideChecked > 1000 );
   CHECK( edgeChecked > 0 );
   CHECK( edgeDiffers );
}

// Regression: a stroke much larger than its automatic sample ring
// (max(32, 3*radius)) used to fail with "no valid source region" at a coarse
// pyramid level, and the interface then silently restored the original
// pixels. With the module defaults (patch 11, feather 3, ring 0) and the
// default brush radius 15, a scribble covering ~200x200 px must fill.
TEST_CASE( stroke_fill_large_stroke_with_small_ring_succeeds )
{
   repatch::Image img = TexturedImage( 400, 400, 11 );
   repatch::Image original = img;
   repatch::Stroke s;
   s.radius = 15; s.softness = 0.5f; s.opacity = 1.0f;
   for ( int row = 0; row <= 13; ++row )
      for ( int i = 0; i <= 20; ++i )
         s.points.push_back( { 100.0f + 10.0f * i, 100.0f + 15.0f * row } );
   repatch::FillParams p; // module defaults
   p.iterations = 2;
   p.randomSeed = repatch::DeriveStrokeSeed( 1, 0 );
   p.threads = 2;
   repatch::FillResult r = repatch::FillStroke( img.View(), s, p );
   REQUIRE( r.ok );
   CHECK( r.levelsUsed >= 1 );
   int changed = 0;
   for ( size_t i = 0; i < img.data.size(); ++i )
      changed += ( img.data[i] != original.data[i] );
   CHECK( changed > 200 * 200 / 2 );
   CHECK( img.At( 0, 2, 2 ) == original.At( 0, 2, 2 ) );
}
