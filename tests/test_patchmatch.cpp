#include "testing.h"
#include "repatch/PatchMatch.h"
#include "repatch/Diffusion.h"

#include <cmath>
#include <cstdio>
#include <string>

// Horizontal period 8 texture with a vertical ramp; hole block at (14..17, 14..17).
static repatch::Level MakeLevel( int w, int h, int r, int hx0, int hy0, int hx1, int hy1,
                                 int searchRadius = 0, int sampleRing = 0, int feather = 0 )
{
   repatch::Level L;
   L.img = repatch::Image( w, h, 1 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
         L.img.At( 0, x, y ) = float( ( x % 8 ) + y ) / 40.0f;
   L.hole.assign( size_t( w ) * h, 0 );
   for ( int y = hy0; y < hy1; ++y )
      for ( int x = hx0; x < hx1; ++x )
      {
         L.hole[size_t( y ) * w + x] = 1;
         L.img.At( 0, x, y ) = 9.0f; // garbage
      }
   L.r = r;
   L.searchRadius = searchRadius;
   L.stretch = nullptr;
   std::string err;
   if ( !repatch::BuildLevelRegions( L, feather, sampleRing, 2, err ) )
   {
      std::printf( "  BuildLevelRegions failed: %s\n", err.c_str() );
      ++testing::Failures();
      L.w = 0; // callers REQUIRE( L.w == ... )
   }
   return L;
}

TEST_CASE( patchmatch_build_level_regions )
{
   repatch::Level L = MakeLevel( 32, 32, 2, 14, 14, 18, 18 );
   REQUIRE( L.w == 32 );
   CHECK( repatch::CountSet( L.hole ) == 16 );
   CHECK( L.synth == L.hole );                       // feather 0
   CHECK( repatch::CountSet( L.target ) == 8 * 8 );  // dilate 4x4 by 2
   CHECK( !L.validList.empty() );
   CHECK( L.match.width == 32 && L.match.channels == 1 );
   CHECK( L.match.data == L.img.data );              // Linear: match == img
}

TEST_CASE( patchmatch_distance_zero_for_identical_patches )
{
   repatch::Level L = MakeLevel( 32, 32, 2, 14, 14, 18, 18 );
   REQUIRE( L.w == 32 );
   CHECK_NEAR( repatch::PatchDistance( L, 5, 5, 13, 5, repatch::kInfDistance ), 0.0, 1e-9 );
   CHECK( repatch::PatchDistance( L, 5, 5, 6, 5, repatch::kInfDistance ) > 0.0f );
   // brute-force check at an image corner (target patch clipped)
   float brute = 0;
   for ( int dy = -2; dy <= 2; ++dy )
      for ( int dx = -2; dx <= 2; ++dx )
      {
         int tx = 0 + dx, ty = 0 + dy;
         if ( tx < 0 || ty < 0 ) continue;
         float d = L.match.At( 0, tx, ty ) - L.match.At( 0, 9 + dx, 7 + dy );
         brute += d * d;
      }
   CHECK_NEAR( repatch::PatchDistance( L, 0, 0, 9, 7, repatch::kInfDistance ), brute, 1e-6 );
   // early exit returns something larger than best
   CHECK( repatch::PatchDistance( L, 5, 5, 6, 5, 0.0f ) > 0.0f );
}

TEST_CASE( patchmatch_candidate_validity )
{
   repatch::Level L = MakeLevel( 32, 32, 2, 14, 14, 18, 18, /*searchRadius*/ 6 );
   REQUIRE( L.w == 32 );
   CHECK( !repatch::IsCandidateValid( L, 15, 15, 15, 15 ) );  // inside hole
   CHECK( !repatch::IsCandidateValid( L, 15, 15, 1, 15 ) );   // patch outside image
   CHECK( repatch::IsCandidateValid( L, 15, 15, 9, 15 ) );    // 6 px away, clear of hole
   CHECK( !repatch::IsCandidateValid( L, 15, 15, 8, 15 ) );   // 7 px away > searchRadius
   CHECK( !repatch::IsCandidateValid( L, 15, 15, -1, 15 ) );
   CHECK( !repatch::IsCandidateValid( L, 15, 15, 15, 40 ) );
}

TEST_CASE( patchmatch_random_valid_source_respects_constraints )
{
   for ( int radius : { 0, 5 } )
   {
      repatch::Level L = MakeLevel( 48, 48, 2, 20, 20, 26, 26, radius );
      REQUIRE( L.w == 48 );
      repatch::Pcg32 rng( 3 );
      for ( int k = 0; k < 500; ++k )
      {
         int sx = -1, sy = -1;
         REQUIRE( repatch::RandomValidSource( L, rng, 21, 21, sx, sy ) );
         CHECK( L.valid[size_t( sy ) * L.w + sx] );
         if ( radius > 0 )
            CHECK( std::abs( sx - 21 ) <= radius && std::abs( sy - 21 ) <= radius );
      }
   }
}

TEST_CASE( patchmatch_init_random_is_valid_and_deterministic )
{
   repatch::Level L = MakeLevel( 48, 48, 2, 20, 20, 26, 26, 7 );
   REQUIRE( L.w == 48 );
   repatch::Nnf a, b;
   repatch::InitNnfRandom( a, L, 0, 42, 1 );
   repatch::InitNnfRandom( b, L, 0, 42, 5 );
   CHECK( a.sx == b.sx && a.sy == b.sy );
   for ( int y = 0; y < L.h; ++y )
      for ( int x = 0; x < L.w; ++x )
      {
         size_t i = size_t( y ) * L.w + x;
         if ( !L.target[i] ) continue;
         CHECK( repatch::IsCandidateValid( L, x, y, a.sx[i], a.sy[i] ) );
      }
   repatch::Nnf c;
   repatch::InitNnfRandom( c, L, 0, 43, 1 );
   CHECK( c.sx != a.sx );
}

TEST_CASE( patchmatch_refresh_distances )
{
   repatch::Level L = MakeLevel( 32, 32, 2, 14, 14, 18, 18 );
   REQUIRE( L.w == 32 );
   repatch::Nnf nnf;
   repatch::InitNnfRandom( nnf, L, 0, 1, 2 );
   repatch::RefreshDistances( nnf, L, 2 );
   for ( int y = 0; y < L.h; ++y )
      for ( int x = 0; x < L.w; ++x )
      {
         size_t i = size_t( y ) * L.w + x;
         if ( !L.target[i] ) continue;
         CHECK_NEAR( nnf.d[i], repatch::PatchDistance( L, x, y, nnf.sx[i], nnf.sy[i], repatch::kInfDistance ), 1e-6 );
      }
}

TEST_CASE( patchmatch_vote_reconstructs_periodic_texture )
{
   repatch::Level L = MakeLevel( 32, 32, 2, 14, 14, 18, 18 );
   REQUIRE( L.w == 32 );
   repatch::Nnf nnf;
   nnf.Resize( L.w, L.h );
   for ( int y = 0; y < L.h; ++y )
      for ( int x = 0; x < L.w; ++x )
      {
         size_t i = size_t( y ) * L.w + x;
         if ( !L.target[i] ) continue;
         nnf.sx[i] = ( x + 8 <= L.w - 1 - L.r ) ? x + 8 : x - 8; // same texture phase, clear of hole
         nnf.sy[i] = y;
      }
   repatch::Vote( nnf, L, L.hole, 3 );
   for ( int y = 0; y < L.h; ++y )
      for ( int x = 0; x < L.w; ++x )
         CHECK_NEAR( L.img.At( 0, x, y ), float( ( x % 8 ) + y ) / 40.0f, 1e-6 );
   // match was refreshed on the hole
   CHECK( L.match.data == L.img.data );

   // out-of-place vote leaves L.img untouched
   repatch::Image copy = L.img;
   repatch::Image out = L.img;
   for ( size_t i = 0; i < out.data.size(); ++i ) out.data[i] = -1.0f;
   repatch::Vote( nnf, L, L.synth, 2, &out );
   CHECK( L.img.data == copy.data );
   CHECK_NEAR( out.At( 0, 15, 15 ), float( ( 15 % 8 ) + 15 ) / 40.0f, 1e-6 );
   CHECK_NEAR( out.At( 0, 0, 0 ), -1.0f, 1e-9 ); // outside region untouched
}

TEST_CASE( patchmatch_refresh_match_applies_stretch )
{
   repatch::Level L = MakeLevel( 32, 32, 2, 14, 14, 18, 18 );
   REQUIRE( L.w == 32 );
   repatch::StretchParams p{ 0.05f, 0.1f };
   L.stretch = &p;
   L.RefreshMatch( nullptr, 2 );
   for ( int y = 0; y < L.h; ++y )
      for ( int x = 0; x < L.w; ++x )
         CHECK_NEAR( L.match.At( 0, x, y ), repatch::ApplyStretch( p, L.img.At( 0, x, y ) ), 1e-6 );
}

// 2-D sinusoidal texture (period 16) with a garbage hole in the middle.
static repatch::Level MakeSineLevel( int w, int h, int r, int holeHalf )
{
   repatch::Level L;
   L.img = repatch::Image( w, h, 1 );
   L.hole.assign( size_t( w ) * h, 0 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
         L.img.At( 0, x, y ) = 0.5f + 0.25f * std::sin( 2 * 3.14159265f * x / 16 ) + 0.2f * std::sin( 2 * 3.14159265f * y / 16 );
         if ( std::abs( x - w / 2 ) < holeHalf && std::abs( y - h / 2 ) < holeHalf )
         {
            L.hole[size_t( y ) * w + x] = 1;
            L.img.At( 0, x, y ) = 0.0f;
         }
      }
   L.r = r;
   L.searchRadius = 0;
   L.stretch = nullptr;
   std::string err;
   if ( !repatch::BuildLevelRegions( L, 0, 0, 2, err ) )
   {
      std::printf( "  BuildLevelRegions failed: %s\n", err.c_str() );
      ++testing::Failures();
      L.w = 0;
   }
   return L;
}

static float SineTruth( int x, int y )
{
   return 0.5f + 0.25f * std::sin( 2 * 3.14159265f * x / 16 ) + 0.2f * std::sin( 2 * 3.14159265f * y / 16 );
}

TEST_CASE( patchmatch_pass_never_increases_distance )
{
   repatch::Level L = MakeSineLevel( 64, 64, 3, 6 );
   REQUIRE( L.w == 64 );
   repatch::DiffusionFill( L.img, L.hole, 2 );
   L.RefreshMatch( &L.hole, 2 );
   repatch::Nnf nnf;
   repatch::InitNnfRandom( nnf, L, 0, 1, 2 );
   repatch::RefreshDistances( nnf, L, 2 );
   std::vector<float> before = nnf.d;
   repatch::PatchMatchPass( nnf, L, 0, 0, 1, 2 );
   double sumBefore = 0, sumAfter = 0;
   for ( size_t i = 0; i < nnf.d.size(); ++i )
      if ( L.target[i] )
      {
         CHECK( nnf.d[i] <= before[i] + 1e-6f );
         CHECK( repatch::IsCandidateValid( L, int( i % L.w ), int( i / L.w ), nnf.sx[i], nnf.sy[i] ) );
         sumBefore += before[i];
         sumAfter += nnf.d[i];
      }
   CHECK( sumAfter < 0.5 * sumBefore );
}

TEST_CASE( patchmatch_passes_and_votes_fill_sine_hole )
{
   repatch::Level L = MakeSineLevel( 64, 64, 3, 6 );
   REQUIRE( L.w == 64 );
   repatch::DiffusionFill( L.img, L.hole, 2 );
   L.RefreshMatch( &L.hole, 2 );
   repatch::Nnf nnf;
   repatch::InitNnfRandom( nnf, L, 0, 5, 2 );
   for ( int pass = 0; pass < 8; ++pass )
   {
      repatch::PatchMatchPass( nnf, L, 0, pass, 5, 2 );
      repatch::Vote( nnf, L, L.hole, 2 );
   }
   double se = 0; int n = 0;
   for ( int y = 0; y < L.h; ++y )
      for ( int x = 0; x < L.w; ++x )
         if ( L.hole[size_t( y ) * L.w + x] )
         {
            double e = L.img.At( 0, x, y ) - SineTruth( x, y );
            se += e * e; ++n;
         }
   CHECK( std::sqrt( se / n ) < 0.06 );
}

TEST_CASE( patchmatch_pass_is_deterministic_across_thread_counts )
{
   repatch::Level A = MakeSineLevel( 70, 50, 3, 7 );
   repatch::Level B = MakeSineLevel( 70, 50, 3, 7 );
   REQUIRE( A.w == 70 && B.w == 70 );
   repatch::DiffusionFill( A.img, A.hole, 1 );
   repatch::DiffusionFill( B.img, B.hole, 4 );
   A.RefreshMatch( &A.hole, 1 );
   B.RefreshMatch( &B.hole, 4 );
   repatch::Nnf na, nb;
   repatch::InitNnfRandom( na, A, 0, 9, 1 );
   repatch::InitNnfRandom( nb, B, 0, 9, 4 );
   for ( int pass = 0; pass < 4; ++pass )
   {
      repatch::PatchMatchPass( na, A, 0, pass, 9, 1 );
      repatch::PatchMatchPass( nb, B, 0, pass, 9, 4 );
      repatch::Vote( na, A, A.hole, 1 );
      repatch::Vote( nb, B, B.hole, 4 );
   }
   CHECK( na.sx == nb.sx && na.sy == nb.sy );
   CHECK( A.img.data == B.img.data );
}

TEST_CASE( patchmatch_upsample_gives_valid_fine_sources )
{
   repatch::Level coarse = MakeSineLevel( 32, 32, 3, 3 );
   repatch::Level fine = MakeSineLevel( 64, 64, 3, 6 );
   REQUIRE( coarse.w == 32 && fine.w == 64 );
   repatch::Nnf cn;
   repatch::InitNnfRandom( cn, coarse, 1, 4, 2 );
   repatch::Nnf fn;
   repatch::UpsampleNnf( cn, coarse, fn, fine, 0, 4, 2 );
   REQUIRE( fn.w == 64 && fn.h == 64 );
   int inherited = 0, targets = 0;
   for ( int y = 0; y < fine.h; ++y )
      for ( int x = 0; x < fine.w; ++x )
      {
         size_t i = size_t( y ) * fine.w + x;
         if ( !fine.target[i] ) continue;
         ++targets;
         CHECK( repatch::IsCandidateValid( fine, x, y, fn.sx[i], fn.sy[i] ) );
         size_t ci = size_t( y / 2 ) * coarse.w + x / 2;
         if ( coarse.target[ci] && fn.sx[i] == 2 * cn.sx[ci] + ( x - 2 * ( x / 2 ) ) && fn.sy[i] == 2 * cn.sy[ci] + ( y - 2 * ( y / 2 ) ) )
            ++inherited;
      }
   CHECK( targets > 0 );
   CHECK( inherited > targets / 2 ); // most fine targets inherit the (valid) coarse mapping
}
