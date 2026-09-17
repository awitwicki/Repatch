#include "testing.h"
#include "repatch/PatchBackend.h"
#include "repatch/Random.h"

#include <cmath>
#include <vector>

namespace {

struct Scene {
   repatch::Image img;
   repatch::Image truth;
   repatch::Mask hole;
   int w = 0, h = 0;
};

// Sinusoidal texture (period 16) per channel with different phases; square
// hole of half-size `half` at the centre filled with garbage.
Scene MakeScene( int w, int h, int channels, int half, float scale = 1.0f, float offset = 0.0f, float noise = 0.0f )
{
   Scene s;
   s.w = w; s.h = h;
   s.truth = repatch::Image( w, h, channels );
   s.hole.assign( size_t( w ) * h, 0 );
   repatch::Pcg32 rng( 77 );
   for ( int c = 0; c < channels; ++c )
      for ( int y = 0; y < h; ++y )
         for ( int x = 0; x < w; ++x )
         {
            float v = 0.5f + 0.25f * std::sin( 2 * 3.14159265f * ( x + 3 * c ) / 16 ) + 0.2f * std::sin( 2 * 3.14159265f * ( y + 5 * c ) / 16 );
            v += noise * ( rng.Uniform() - 0.5f );
            s.truth.At( c, x, y ) = offset + scale * v;
         }
   s.img = s.truth;
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
         if ( std::abs( x - w / 2 ) < half && std::abs( y - h / 2 ) < half )
         {
            s.hole[size_t( y ) * w + x] = 1;
            for ( int c = 0; c < channels; ++c )
               s.img.At( c, x, y ) = 0.0f;
         }
   return s;
}

repatch::FillRequest MakeRequest( Scene& s, const repatch::FillParams& p )
{
   repatch::FillRequest r;
   r.image = s.img.View();
   r.mask = s.hole.data();
   r.params = p;
   return r;
}

double HoleRmse( const Scene& s )
{
   double se = 0; int n = 0;
   for ( int c = 0; c < s.img.channels; ++c )
      for ( int y = 0; y < s.h; ++y )
         for ( int x = 0; x < s.w; ++x )
            if ( s.hole[size_t( y ) * s.w + x] )
            {
               double e = s.img.At( c, x, y ) - s.truth.At( c, x, y );
               se += e * e; ++n;
            }
   return std::sqrt( se / n );
}

repatch::FillParams BaseParams()
{
   repatch::FillParams p;
   p.patchSize = 9;
   p.iterations = 5;
   p.randomSeed = 7;
   p.feather = 0;
   p.matchSpace = repatch::MatchSpace::Linear;
   p.threads = 2;
   return p;
}

} // namespace

TEST_CASE( backend_rejects_invalid_requests )
{
   repatch::PatchBackend backend;
   Scene s = MakeScene( 64, 64, 1, 6 );

   repatch::FillParams p = BaseParams();
   p.patchSize = 10;
   repatch::FillRequest r = MakeRequest( s, p );
   CHECK( !backend.Fill( r ).ok ); // even patch size

   p = BaseParams();
   r = MakeRequest( s, p );
   r.image.channels = 2;
   CHECK( !backend.Fill( r ).ok );

   r = MakeRequest( s, p );
   r.mask = nullptr;
   CHECK( !backend.Fill( r ).ok );

   repatch::Mask empty( s.hole.size(), 0 );
   r = MakeRequest( s, p );
   r.mask = empty.data();
   repatch::FillResult res = backend.Fill( r );
   CHECK( !res.ok );
   CHECK( !res.error.empty() );

   repatch::Mask full( s.hole.size(), 1 );
   r = MakeRequest( s, p );
   r.mask = full.data();
   CHECK( !backend.Fill( r ).ok );

   // Nothing fits: 20x20 image, 10x10 hole, patch 11.
   Scene tiny = MakeScene( 20, 20, 1, 5 );
   p = BaseParams();
   p.patchSize = 11;
   r = MakeRequest( tiny, p );
   res = backend.Fill( r );
   CHECK( !res.ok );
   CHECK( res.error.find( "no valid source region" ) != std::string::npos );
}

TEST_CASE( backend_fills_periodic_texture_mono )
{
   Scene s = MakeScene( 96, 96, 1, 12 );
   repatch::Image original = s.img;
   repatch::FillRequest r = MakeRequest( s, BaseParams() );
   repatch::PatchBackend backend;
   repatch::FillResult res = backend.Fill( r );
   REQUIRE( res.ok );
   CHECK( res.seedUsed == 7 );
   CHECK( res.levelsUsed >= 1 );
   CHECK( res.seconds >= 0 );
   CHECK( HoleRmse( s ) < 0.06 );
   for ( size_t i = 0; i < s.hole.size(); ++i )
      if ( !s.hole[i] )
         CHECK( s.img.data[i] == original.data[i] ); // feather 0: known pixels bit-identical
}

TEST_CASE( backend_fills_periodic_texture_rgb )
{
   Scene s = MakeScene( 96, 96, 3, 12 );
   repatch::FillRequest r = MakeRequest( s, BaseParams() );
   repatch::PatchBackend backend;
   repatch::FillResult res = backend.Fill( r );
   REQUIRE( res.ok );
   CHECK( HoleRmse( s ) < 0.07 );
}

TEST_CASE( backend_is_deterministic_across_thread_counts )
{
   std::vector<float> outputs[3];
   int threads[3] = { 1, 2, 7 };
   for ( int k = 0; k < 3; ++k )
   {
      Scene s = MakeScene( 80, 72, 3, 10, 1.0f, 0.0f, 0.05f );
      repatch::FillParams p = BaseParams();
      p.threads = threads[k];
      p.feather = 3;
      p.matchSpace = repatch::MatchSpace::Stretched;
      repatch::FillRequest r = MakeRequest( s, p );
      repatch::PatchBackend backend;
      REQUIRE( backend.Fill( r ).ok );
      outputs[k] = s.img.data;
   }
   CHECK( outputs[0] == outputs[1] );
   CHECK( outputs[0] == outputs[2] );
}

TEST_CASE( backend_seed_zero_reports_reproducible_seed )
{
   Scene a = MakeScene( 64, 64, 1, 8 );
   repatch::FillParams p = BaseParams();
   p.randomSeed = 0;
   repatch::FillRequest ra = MakeRequest( a, p );
   repatch::PatchBackend backend;
   repatch::FillResult res = backend.Fill( ra );
   REQUIRE( res.ok );
   CHECK( res.seedUsed != 0 );

   Scene b = MakeScene( 64, 64, 1, 8 );
   p.randomSeed = res.seedUsed;
   repatch::FillRequest rb = MakeRequest( b, p );
   REQUIRE( backend.Fill( rb ).ok );
   CHECK( a.img.data == b.img.data );
}

TEST_CASE( backend_honours_source_constraints )
{
   Scene s = MakeScene( 128, 128, 1, 10 );
   repatch::FillParams p = BaseParams();
   p.patchSize = 7;
   p.sampleRing = 10;
   p.searchRadius = 24;
   p.feather = 2;
   repatch::FillRequest r = MakeRequest( s, p );
   repatch::PatchBackend backend;
   backend.EnableDebugCapture();
   REQUIRE( backend.Fill( r ).ok );
   const repatch::PatchBackend::DebugInfo& D = backend.Debug();
   REQUIRE( D.width > 0 && D.height > 0 );
   const int rr = D.r;
   CHECK( rr == 3 );
   std::vector<float> dist = repatch::DistanceTransform( D.hole, D.width, D.height, 2 );
   int checked = 0;
   for ( int y = 0; y < D.height; ++y )
      for ( int x = 0; x < D.width; ++x )
      {
         size_t i = size_t( y ) * D.width + x;
         if ( !D.target[i] ) continue;
         ++checked;
         int sx = D.nnf.sx[i], sy = D.nnf.sy[i];
         CHECK( D.valid[size_t( sy ) * D.width + sx] );
         CHECK( std::abs( sx - x ) <= 24 && std::abs( sy - y ) <= 24 );
         CHECK( dist[size_t( sy ) * D.width + sx] <= 10.0f + 1e-4f );
         for ( int dy = -rr; dy <= rr; ++dy )
            for ( int dx = -rr; dx <= rr; ++dx )
               CHECK( D.hole[size_t( sy + dy ) * D.width + ( sx + dx )] == 0 );
      }
   CHECK( checked > 0 );
   // With bounded sampling the working region is cropped: ROI must be smaller than the image.
   CHECK( D.width < 128 && D.height < 128 );
   CHECK( D.roiX0 > 0 && D.roiY0 > 0 );
}

TEST_CASE( backend_stretched_mode_outputs_linear_values )
{
   // Linear astro-like range: values in [1.5e-4, 1.05e-3].
   Scene s = MakeScene( 96, 96, 1, 10, 1e-3f, 1e-4f );
   repatch::FillParams p = BaseParams();
   p.matchSpace = repatch::MatchSpace::Stretched;
   repatch::FillRequest r = MakeRequest( s, p );
   repatch::PatchBackend backend;
   REQUIRE( backend.Fill( r ).ok );
   float lo = 1e9f, hi = -1e9f;
   for ( size_t i = 0; i < s.hole.size(); ++i )
      if ( !s.hole[i] ) { lo = std::min( lo, s.truth.data[i] ); hi = std::max( hi, s.truth.data[i] ); }
   for ( size_t i = 0; i < s.hole.size(); ++i )
      if ( s.hole[i] )
      {
         CHECK( s.img.data[i] >= lo - 1e-6f );
         CHECK( s.img.data[i] <= hi + 1e-6f );
      }
   CHECK( HoleRmse( s ) < 1e-4 );
}

TEST_CASE( backend_feather_touches_only_the_ring )
{
   Scene s = MakeScene( 96, 96, 1, 8, 1.0f, 0.0f, 0.2f );
   repatch::Image original = s.img;
   repatch::FillParams p = BaseParams();
   p.feather = 3;
   repatch::FillRequest r = MakeRequest( s, p );
   repatch::PatchBackend backend;
   REQUIRE( backend.Fill( r ).ok );
   std::vector<float> dist = repatch::DistanceTransform( s.hole, s.w, s.h, 2 );
   int changedRing = 0;
   for ( size_t i = 0; i < s.hole.size(); ++i )
   {
      if ( s.hole[i] ) continue;
      if ( dist[i] > 3.0f )
         CHECK( s.img.data[i] == original.data[i] );
      else if ( s.img.data[i] != original.data[i] )
         ++changedRing;
   }
   CHECK( changedRing > 0 );
}

TEST_CASE( backend_abort_leaves_image_untouched )
{
   Scene s = MakeScene( 64, 64, 1, 8 );
   repatch::Image original = s.img;
   repatch::FillRequest r = MakeRequest( s, BaseParams() );
   repatch::PatchBackend backend;
   int calls = 0;
   repatch::FillResult res = backend.Fill( r, [&]( float, const char* ) { ++calls; return false; } );
   CHECK( !res.ok );
   CHECK( res.aborted );
   CHECK( calls == 1 );
   CHECK( s.img.data == original.data );
}

TEST_CASE( backend_progress_is_monotone_and_reports_levels )
{
   Scene s = MakeScene( 300, 300, 1, 44 ); // 87x87 hole
   repatch::FillParams p = BaseParams();
   p.patchSize = 11;
   p.iterations = 2;
   repatch::FillRequest r = MakeRequest( s, p );
   repatch::PatchBackend backend;
   float last = -1; bool monotone = true; int calls = 0;
   repatch::FillResult res = backend.Fill( r, [&]( float f, const char* stage ) {
      if ( f < last || f < 0 || f > 1 ) monotone = false;
      last = f; ++calls;
      CHECK( stage != nullptr );
      return true;
   } );
   REQUIRE( res.ok );
   CHECK( monotone );
   // hole is |x-150| < 44 -> 87 px wide; 87/11 = 7.9 -> 1 + floor(log2(7.9)) = 3 levels
   // (cap: 300 -> 150 -> 75, all >= 22, so the cap does not reduce it)
   CHECK( res.levelsUsed == 3 );
   CHECK( calls == res.levelsUsed * 2 + 1 ); // one call per pass plus "done"
}

TEST_CASE( backend_weight_null_equals_all_ones )
{
   Scene a = MakeScene( 96, 96, 1, 12 );
   Scene b = MakeScene( 96, 96, 1, 12 );
   repatch::FillParams p = BaseParams();
   std::vector<float> ones( size_t( b.w ) * b.h, 1.0f );
   repatch::FillRequest ra = MakeRequest( a, p );
   repatch::FillRequest rb = MakeRequest( b, p );
   rb.weight = ones.data();
   repatch::PatchBackend backend;
   REQUIRE( backend.Fill( ra ).ok );
   REQUIRE( backend.Fill( rb ).ok );
   CHECK( a.img.data == b.img.data );
}

TEST_CASE( backend_weight_blends_hole_pixels )
{
   Scene full = MakeScene( 96, 96, 3, 12 );
   Scene half = MakeScene( 96, 96, 3, 12 );
   repatch::Image original = half.img;
   repatch::FillParams p = BaseParams();
   std::vector<float> w( size_t( half.w ) * half.h, 0.5f );
   repatch::FillRequest rf = MakeRequest( full, p );
   repatch::FillRequest rh = MakeRequest( half, p );
   rh.weight = w.data();
   repatch::PatchBackend backend;
   REQUIRE( backend.Fill( rf ).ok );
   REQUIRE( backend.Fill( rh ).ok );
   for ( int c = 0; c < 3; ++c )
      for ( size_t i = 0; i < half.hole.size(); ++i )
      {
         size_t k = size_t( c ) * half.hole.size() + i;
         if ( half.hole[i] )
            CHECK_NEAR( half.img.data[k], 0.5f * full.img.data[k] + 0.5f * original.data[k], 1e-6 );
         else
            CHECK( half.img.data[k] == original.data[k] );
      }
}

TEST_CASE( backend_weight_zero_leaves_hole_unchanged )
{
   Scene s = MakeScene( 64, 64, 1, 8 );
   repatch::Image original = s.img;
   std::vector<float> w( size_t( s.w ) * s.h, 0.0f );
   repatch::FillRequest r = MakeRequest( s, BaseParams() );
   r.weight = w.data();
   repatch::PatchBackend backend;
   REQUIRE( backend.Fill( r ).ok );
   CHECK( s.img.data == original.data );
}

TEST_CASE( backend_weight_is_clamped )
{
   Scene s = MakeScene( 64, 64, 1, 8 );
   repatch::Image original = s.img;
   std::vector<float> w( size_t( s.w ) * s.h, 7.0f ); // clamps to 1
   Scene ref = MakeScene( 64, 64, 1, 8 );
   repatch::FillRequest r = MakeRequest( s, BaseParams() );
   r.weight = w.data();
   repatch::FillRequest rr = MakeRequest( ref, BaseParams() );
   repatch::PatchBackend backend;
   REQUIRE( backend.Fill( r ).ok );
   REQUIRE( backend.Fill( rr ).ok );
   CHECK( s.img.data == ref.img.data );
}

// Regression: the pyramid depth comes from the hole size, but with a bounded
// sample ring the working region is only ring + patch around the hole. At
// coarse levels that margin halves while the patch radius does not, so a
// level can end up with no valid source centre at all. Such levels must be
// dropped (levelsUsed < requested), not fail the whole fill.
TEST_CASE( backend_drops_coarse_levels_without_sources )
{
   Scene s = MakeScene( 400, 400, 1, 150 ); // 299x299 hole
   repatch::Image original = s.img;
   repatch::FillParams p = BaseParams();
   p.patchSize = 11;
   p.sampleRing = 45; // brush default for radius 15
   p.iterations = 2;
   repatch::FillRequest r = MakeRequest( s, p );
   repatch::PatchBackend backend;
   repatch::FillResult res = backend.Fill( r );
   REQUIRE( res.ok );
   // Auto would ask for 1 + floor(log2(299/11)) = 5 levels; the coarse ones
   // have no room for a source patch inside the 56 px margin.
   CHECK( res.levelsUsed >= 1 );
   CHECK( res.levelsUsed < 5 );
   int changed = 0;
   for ( size_t i = 0; i < s.hole.size(); ++i )
      if ( s.hole[i] && s.img.data[i] != original.data[i] )
         ++changed;
   CHECK( changed > 299 * 299 / 2 );

   // An explicit level count is a cap as well: the same request with
   // pyramidLevels = 5 succeeds with the same number of levels.
   Scene s2 = MakeScene( 400, 400, 1, 150 );
   p.pyramidLevels = 5;
   r = MakeRequest( s2, p );
   repatch::FillResult res2 = backend.Fill( r );
   REQUIRE( res2.ok );
   CHECK( res2.levelsUsed == res.levelsUsed );
   CHECK( s2.img.data == s.img.data );

   // The finest level still needs a source region; that remains an error.
   Scene tiny = MakeScene( 20, 20, 1, 5 );
   p = BaseParams();
   p.patchSize = 11;
   r = MakeRequest( tiny, p );
   res = backend.Fill( r );
   CHECK( !res.ok );
   CHECK( res.error.find( "no valid source region" ) != std::string::npos );
}
