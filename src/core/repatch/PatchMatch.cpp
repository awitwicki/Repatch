#include "PatchMatch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Parallel.h"

namespace repatch {

void Level::RefreshMatch( const Mask* region, int threads )
{
   for ( int c = 0; c < img.channels; ++c )
   {
      const float* I = img.Plane( c );
      float* M = match.Plane( c );
      const StretchParams* p = stretch ? &stretch[c] : nullptr;
      ParallelRows( h, threads, [&]( int y ) {
         for ( int x = 0; x < w; ++x )
         {
            std::size_t i = std::size_t( y ) * w + x;
            if ( region && !( *region )[i] )
               continue;
            M[i] = p ? ApplyStretch( *p, I[i] ) : I[i];
         }
      } );
   }
}

bool BuildLevelRegions( Level& L, int feather, int sampleRing, int threads, std::string& err )
{
   L.w = L.img.width;
   L.h = L.img.height;
   L.holeDist = DistanceTransform( L.hole, L.w, L.h, threads );
   L.synth = ( feather > 0 ) ? ThresholdDistance( L.holeDist, float( feather ) ) : L.hole;
   L.target = DilateChebyshev( L.synth, L.w, L.h, L.r );
   L.valid = ValidSourceMap( L.hole, L.w, L.h, L.r, L.holeDist, sampleRing );
   L.validList.clear();
   for ( std::size_t i = 0; i < L.valid.size(); ++i )
      if ( L.valid[i] )
         L.validList.push_back( int32_t( i ) );
   if ( L.validList.empty() )
   {
      err = "no valid source region: every candidate patch overlaps the hole, lies outside the "
            "sampling ring, or does not fit in the image; reduce patchSize or pyramidLevels, "
            "or increase sampleRing";
      return false;
   }
   if ( L.searchRadius > 0 )
   {
      Mask reach = DilateChebyshev( L.valid, L.w, L.h, L.searchRadius );
      for ( std::size_t i = 0; i < reach.size(); ++i )
         if ( L.target[i] && !reach[i] )
         {
            err = "searchRadius too small: some pixels near the hole have no valid source patch "
                  "within the search radius";
            return false;
         }
   }
   L.match = Image( L.w, L.h, L.img.channels );
   L.RefreshMatch( nullptr, threads );
   return true;
}

float PatchDistance( const Level& L, int tx, int ty, int sx, int sy, float best )
{
   const int r = L.r, w = L.w, h = L.h;
   const int dy0 = std::max( -r, -ty ), dy1 = std::min( r, h - 1 - ty );
   const int dx0 = std::max( -r, -tx ), dx1 = std::min( r, w - 1 - tx );
   float sum = 0;
   for ( int c = 0; c < L.match.channels; ++c )
   {
      const float* P = L.match.Plane( c );
      for ( int dy = dy0; dy <= dy1; ++dy )
      {
         const float* tRow = P + std::size_t( ty + dy ) * w + tx;
         const float* sRow = P + std::size_t( sy + dy ) * w + sx;
         for ( int dx = dx0; dx <= dx1; ++dx )
         {
            float d = tRow[dx] - sRow[dx];
            sum += d * d;
         }
         if ( sum > best )
            return sum;
      }
   }
   return sum;
}

bool IsCandidateValid( const Level& L, int tx, int ty, int sx, int sy )
{
   if ( sx < 0 || sy < 0 || sx >= L.w || sy >= L.h )
      return false;
   if ( !L.valid[std::size_t( sy ) * L.w + sx] )
      return false;
   if ( L.searchRadius > 0 )
      if ( std::abs( sx - tx ) > L.searchRadius || std::abs( sy - ty ) > L.searchRadius )
         return false;
   return true;
}

bool RandomValidSource( const Level& L, Pcg32& rng, int tx, int ty, int& sx, int& sy )
{
   if ( L.searchRadius == 0 )
   {
      int32_t i = L.validList[rng.Below( uint32_t( L.validList.size() ) )];
      sx = i % L.w;
      sy = i / L.w;
      return true;
   }
   const int R = L.searchRadius, r = L.r;
   const int x0 = std::max( r, tx - R ), x1 = std::min( L.w - 1 - r, tx + R );
   const int y0 = std::max( r, ty - R ), y1 = std::min( L.h - 1 - r, ty + R );
   if ( x0 > x1 || y0 > y1 )
      return false;
   for ( int k = 0; k < 64; ++k )
   {
      int x = rng.Range( x0, x1 ), y = rng.Range( y0, y1 );
      if ( L.valid[std::size_t( y ) * L.w + x] )
      {
         sx = x; sy = y;
         return true;
      }
   }
   // Exhaustive fallback over the window, starting at a random offset.
   const int nx = x1 - x0 + 1, ny = y1 - y0 + 1, n = nx * ny;
   const int start = int( rng.Below( uint32_t( n ) ) );
   for ( int k = 0; k < n; ++k )
   {
      int idx = ( start + k ) % n;
      int x = x0 + idx % nx, y = y0 + idx / nx;
      if ( L.valid[std::size_t( y ) * L.w + x] )
      {
         sx = x; sy = y;
         return true;
      }
   }
   return false;
}

void InitNnfRandom( Nnf& nnf, const Level& L, int levelIndex, uint32_t seed, int threads )
{
   nnf.Resize( L.w, L.h );
   ParallelRows( L.h, threads, [&]( int y ) {
      Pcg32 rng( HashSeed( seed, 0x1000u + uint32_t( levelIndex ), 0u, uint32_t( y ) ) );
      for ( int x = 0; x < L.w; ++x )
      {
         std::size_t i = std::size_t( y ) * L.w + x;
         if ( !L.target[i] )
            continue;
         int sx, sy;
         if ( !RandomValidSource( L, rng, x, y, sx, sy ) )
         {
            sx = L.validList[0] % L.w;
            sy = L.validList[0] / L.w;
         }
         nnf.sx[i] = sx;
         nnf.sy[i] = sy;
         nnf.d[i] = kInfDistance;
      }
   } );
}

void RefreshDistances( Nnf& nnf, const Level& L, int threads )
{
   ParallelRows( L.h, threads, [&]( int y ) {
      for ( int x = 0; x < L.w; ++x )
      {
         std::size_t i = std::size_t( y ) * L.w + x;
         if ( L.target[i] )
            nnf.d[i] = PatchDistance( L, x, y, nnf.sx[i], nnf.sy[i], kInfDistance );
      }
   } );
}

void Vote( const Nnf& nnf, Level& L, const Mask& region, int threads, Image* out )
{
   const int w = L.w, h = L.h, r = L.r, C = L.img.channels;
   const Image& src = L.img;
   Image& dst = out ? *out : L.img;
   ParallelRows( h, threads, [&]( int y ) {
      for ( int x = 0; x < w; ++x )
      {
         std::size_t i = std::size_t( y ) * w + x;
         if ( !region[i] )
            continue;
         float acc[3] = { 0, 0, 0 };
         int n = 0;
         const int qy0 = std::max( 0, y - r ), qy1 = std::min( h - 1, y + r );
         const int qx0 = std::max( 0, x - r ), qx1 = std::min( w - 1, x + r );
         for ( int qy = qy0; qy <= qy1; ++qy )
            for ( int qx = qx0; qx <= qx1; ++qx )
            {
               std::size_t q = std::size_t( qy ) * w + qx;
               if ( !L.target[q] )
                  continue;
               int px = nnf.sx[q] + ( x - qx );
               int py = nnf.sy[q] + ( y - qy );
               std::size_t p = std::size_t( py ) * w + px;
               for ( int c = 0; c < C; ++c )
                  acc[c] += src.Plane( c )[p];
               ++n;
            }
         if ( n > 0 )
            for ( int c = 0; c < C; ++c )
               dst.Plane( c )[i] = acc[c] / float( n );
      }
   } );
   if ( !out )
      L.RefreshMatch( &region, threads );
}

void PatchMatchPass( Nnf& nnf, const Level& L, int levelIndex, int pass, uint32_t seed, int threads )
{
   RefreshDistances( nnf, L, threads );

   const bool forward = ( pass % 2 ) == 0;
   const int shift = forward ? 0 : kBandHeight / 2;
   const int w = L.w, h = L.h;
   const int maxRadius = ( L.searchRadius > 0 ) ? L.searchRadius : std::max( w, h );
   const int dn = forward ? -1 : 1; // offset to the already-visited neighbour on each axis

   ParallelBands( h, shift, threads, [&]( int y0, int y1, int band ) {
      Pcg32 rng( HashSeed( seed, uint32_t( levelIndex ), uint32_t( pass ), uint32_t( band ) ) );
      const int yBegin = forward ? y0 : y1 - 1;
      const int yEnd = forward ? y1 : y0 - 1;
      const int step = forward ? 1 : -1;
      for ( int y = yBegin; y != yEnd; y += step )
      {
         const int ny = y + dn;
         const bool vertOk = ( ny >= y0 && ny < y1 ); // never propagate across a band boundary
         const int xBegin = forward ? 0 : w - 1;
         const int xEnd = forward ? w : -1;
         for ( int x = xBegin; x != xEnd; x += step )
         {
            const std::size_t i = std::size_t( y ) * w + x;
            if ( !L.target[i] )
               continue;
            int bx = nnf.sx[i], by = nnf.sy[i];
            float best = nnf.d[i];
            auto tryCandidate = [&]( int cx, int cy ) {
               if ( cx == bx && cy == by )
                  return;
               if ( !IsCandidateValid( L, x, y, cx, cy ) )
                  return;
               float d = PatchDistance( L, x, y, cx, cy, best );
               if ( d < best )
               {
                  best = d; bx = cx; by = cy;
               }
            };

            // Propagation from the horizontal scan-order neighbour. A known
            // (non-target) neighbour maps to itself, so its proposal is the
            // identity offset.
            const int nx = x + dn;
            if ( nx >= 0 && nx < w )
            {
               const std::size_t n = std::size_t( y ) * w + nx;
               if ( L.target[n] )
                  tryCandidate( nnf.sx[n] - dn, nnf.sy[n] );
               else
                  tryCandidate( x, y );
            }
            // Propagation from the vertical scan-order neighbour.
            if ( vertOk )
            {
               const std::size_t n = std::size_t( ny ) * w + x;
               if ( L.target[n] )
                  tryCandidate( nnf.sx[n], nnf.sy[n] - dn );
               else
                  tryCandidate( x, y );
            }
            // Random search: window halves each step (alpha = 1/2).
            for ( int R = maxRadius; R >= 1; R /= 2 )
            {
               int cx = bx + rng.Range( -R, R );
               int cy = by + rng.Range( -R, R );
               tryCandidate( cx, cy );
            }

            nnf.sx[i] = bx;
            nnf.sy[i] = by;
            nnf.d[i] = best;
         }
      }
   } );
}

void UpsampleNnf( const Nnf& coarse, const Level& coarseL, Nnf& fine, const Level& fineL,
                  int levelIndex, uint32_t seed, int threads )
{
   fine.Resize( fineL.w, fineL.h );
   ParallelRows( fineL.h, threads, [&]( int y ) {
      Pcg32 rng( HashSeed( seed, 0x5000u + uint32_t( levelIndex ), 0u, uint32_t( y ) ) );
      for ( int x = 0; x < fineL.w; ++x )
      {
         std::size_t i = std::size_t( y ) * fineL.w + x;
         if ( !fineL.target[i] )
            continue;
         const int cx = std::min( x / 2, coarseL.w - 1 );
         const int cy = std::min( y / 2, coarseL.h - 1 );
         const std::size_t ci = std::size_t( cy ) * coarseL.w + cx;
         int sx = 0, sy = 0;
         bool ok = false;
         if ( coarseL.target[ci] )
         {
            sx = 2 * coarse.sx[ci] + ( x - 2 * cx );
            sy = 2 * coarse.sy[ci] + ( y - 2 * cy );
            ok = IsCandidateValid( fineL, x, y, sx, sy );
         }
         if ( !ok )
            ok = RandomValidSource( fineL, rng, x, y, sx, sy );
         if ( !ok )
         {
            sx = fineL.validList[0] % fineL.w;
            sy = fineL.validList[0] / fineL.w;
         }
         fine.sx[i] = sx;
         fine.sy[i] = sy;
         fine.d[i] = kInfDistance;
      }
   } );
}

} // namespace repatch
