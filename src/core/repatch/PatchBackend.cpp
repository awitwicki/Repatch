#include "PatchBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <new>
#include <random>
#include <string>

#include "Diffusion.h"
#include "Parallel.h"
#include "Pyramid.h"

namespace repatch {

namespace {

const char* ValidateRequest( const FillRequest& req )
{
   const ImageView& v = req.image;
   const FillParams& p = req.params;
   if ( v.width <= 0 || v.height <= 0 )
      return "image is empty";
   if ( v.channels != 1 && v.channels != 3 )
      return "image must have 1 or 3 channels";
   for ( int c = 0; c < v.channels; ++c )
      if ( !v.plane[c] )
         return "image plane pointer is null";
   if ( !req.mask )
      return "mask pointer is null";
   if ( p.patchSize < 5 || p.patchSize > 41 || ( p.patchSize % 2 ) == 0 )
      return "patchSize must be odd and in 5..41";
   if ( p.pyramidLevels < 0 || p.pyramidLevels > 8 )
      return "pyramidLevels must be in 0..8 (0 = auto)";
   if ( p.iterations < 1 || p.iterations > 50 )
      return "iterations must be in 1..50";
   if ( p.searchRadius < 0 || p.sampleRing < 0 )
      return "searchRadius and sampleRing must be >= 0";
   if ( p.feather < 0 || p.feather > 256 )
      return "feather must be in 0..256";
   return nullptr;
}

// Copies the ROI out of the caller's planes, replacing non-finite samples by 0.
Image CropFromView( const ImageView& v, const BBox& roi )
{
   Image img( roi.Width(), roi.Height(), v.channels );
   for ( int c = 0; c < v.channels; ++c )
      for ( int y = 0; y < img.height; ++y )
      {
         const float* src = v.plane[c] + std::size_t( roi.y0 + y ) * v.width + roi.x0;
         float* dst = img.Plane( c ) + std::size_t( y ) * img.width;
         for ( int x = 0; x < img.width; ++x )
            dst[x] = IsFiniteSample( src[x] ) ? src[x] : 0.0f;
      }
   return img;
}

// Writes only the pixels of `region` back into the caller's planes. Hole
// pixels are composited with the optional weight map; the caller's buffer
// still holds the original values at this point, so no extra copy is needed.
void WriteBackRegion( const Image& img, const Mask& region, const Mask& hole,
                      const float* weight, const BBox& roi, ImageView& v )
{
   for ( int c = 0; c < v.channels; ++c )
      for ( int y = 0; y < img.height; ++y )
      {
         const float* src = img.Plane( c ) + std::size_t( y ) * img.width;
         const uint8_t* reg = region.data() + std::size_t( y ) * img.width;
         const uint8_t* hol = hole.data() + std::size_t( y ) * img.width;
         const std::size_t rowV = std::size_t( roi.y0 + y ) * v.width + roi.x0;
         float* dst = v.plane[c] + rowV;
         const float* wrow = weight ? weight + rowV : nullptr;
         for ( int x = 0; x < img.width; ++x )
         {
            if ( !reg[x] )
               continue;
            if ( wrow && hol[x] )
            {
               float a = std::min( 1.0f, std::max( 0.0f, wrow[x] ) );
               dst[x] = a * src[x] + ( 1.0f - a ) * dst[x];
            }
            else
               dst[x] = src[x];
         }
      }
}

} // namespace

FillResult PatchBackend::Fill( FillRequest& req, const ProgressFn& progress )
{
   using clock = std::chrono::steady_clock;
   const auto t0 = clock::now();
   FillResult res;
   auto finish = [&]( FillResult r ) {
      r.seconds = std::chrono::duration<double>( clock::now() - t0 ).count();
      return r;
   };
   auto fail = [&]( const std::string& msg ) {
      res.ok = false;
      res.error = msg;
      return finish( res );
   };

   try
   {
      if ( const char* e = ValidateRequest( req ) )
         return fail( e );

      const FillParams& P = req.params;
      ImageView& V = req.image;
      const int W = V.width, H = V.height, C = V.channels;
      const int r = P.patchSize / 2;
      const int threads = ResolveThreads( P.threads );

      uint32_t seed = P.randomSeed;
      if ( seed == 0 )
      {
         std::random_device rd;
         do seed = rd(); while ( seed == 0 );
      }
      res.seedUsed = seed;

      Mask holeFull( V.Pixels() );
      for ( std::size_t i = 0; i < holeFull.size(); ++i )
         holeFull[i] = req.mask[i] ? 1 : 0;
      const BBox bb = MaskBBox( holeFull, W, H );
      if ( bb.Empty() )
         return fail( "mask has no hole pixels (nothing to fill)" );
      if ( CountSet( holeFull ) == holeFull.size() )
         return fail( "mask covers the whole image; there are no source pixels" );

      // Stretch parameters come from the full image's known pixels.
      const bool stretched = ( P.matchSpace == MatchSpace::Stretched );
      StretchParams stretch[3];
      if ( stretched )
         for ( int c = 0; c < C; ++c )
            stretch[c] = ComputeAutoStretch( V.plane[c], W, H, holeFull );

      // Working region: the whole image, unless sampling is spatially bounded,
      // in which case nothing outside bbox(hole) + margins can matter.
      BBox roi{ 0, 0, W, H };
      if ( P.sampleRing > 0 || P.searchRadius > 0 )
      {
         const int expand = P.feather + 2 * r + std::max( P.sampleRing, P.searchRadius ) + 1;
         roi.x0 = std::max( 0, bb.x0 - expand );
         roi.y0 = std::max( 0, bb.y0 - expand );
         roi.x1 = std::min( W, bb.x1 + expand );
         roi.y1 = std::min( H, bb.y1 + expand );
      }

      // The requested level count is a cap. CapPyramidLevels keeps the
      // coarsest level at least two patches wide; the loop below drops any
      // coarse level that has no usable source region, which happens when a
      // bounded sample ring or search radius shrinks with the level while the
      // patch radius does not (a brush stroke much larger than its ring, for
      // instance). Only the finest level must have sources.
      int levels = ( P.pyramidLevels > 0 ) ? P.pyramidLevels : AutoPyramidLevels( bb, P.patchSize );
      levels = CapPyramidLevels( levels, roi.Width(), roi.Height(), P.patchSize );

      // Build the pyramid, one level at a time.
      std::vector<Level> L;
      L.reserve( levels );
      for ( int k = 0; k < levels; ++k )
      {
         Level lv;
         if ( k == 0 )
         {
            lv.img = CropFromView( V, roi );
            lv.hole = CropMask( holeFull, W, H, roi );
         }
         else
         {
            lv.img = DownsampleImage( L[k - 1].img, threads );
            lv.hole = DownsampleMask( L[k - 1].hole, L[k - 1].img.width, L[k - 1].img.height );
         }
         lv.r = r;
         lv.searchRadius = ( P.searchRadius > 0 ) ? std::max( 1, P.searchRadius >> k ) : 0;
         lv.stretch = stretched ? stretch : nullptr;
         const int ring = ( P.sampleRing > 0 ) ? std::max( 1, P.sampleRing >> k ) : 0;
         std::string err;
         if ( !BuildLevelRegions( lv, ( k == 0 ) ? P.feather : 0, ring, threads, err ) )
         {
            if ( k == 0 )
               return fail( err );
            break; // coarser levels would only be worse; stop the pyramid here
         }
         L.push_back( std::move( lv ) );
      }
      levels = int( L.size() );
      res.levelsUsed = levels;

      // Progress bookkeeping: one unit per target pixel per pass.
      std::vector<double> targetCount( levels );
      double total = 0;
      for ( int k = 0; k < levels; ++k )
      {
         targetCount[k] = double( CountSet( L[k].target ) );
         total += P.iterations * targetCount[k];
      }
      double done = 0;
      auto report = [&]( const char* stage ) -> bool {
         if ( !progress )
            return true;
         return progress( ( total > 0 ) ? float( done / total ) : 1.0f, stage );
      };

      // Coarsest level: diffusion initialisation and a random NNF.
      const int top = levels - 1;
      DiffusionFill( L[top].img, L[top].hole, threads );
      L[top].RefreshMatch( &L[top].hole, threads );
      Nnf nnf;
      InitNnfRandom( nnf, L[top], top, seed, threads );

      for ( int k = top; k >= 0; --k )
      {
         if ( k < top )
         {
            Nnf fine;
            UpsampleNnf( nnf, L[k + 1], fine, L[k], k, seed, threads );
            nnf = std::move( fine );
            Vote( nnf, L[k], L[k].hole, threads );
         }
         for ( int it = 0; it < P.iterations; ++it )
         {
            char stage[64];
            std::snprintf( stage, sizeof stage, "level %d/%d, pass %d/%d", levels - k, levels, it + 1, P.iterations );
            if ( !report( stage ) )
            {
               res.aborted = true;
               res.error = "aborted";
               return finish( res );
            }
            PatchMatchPass( nnf, L[k], k, it, seed, threads );
            Vote( nnf, L[k], L[k].hole, threads );
            done += targetCount[k];
         }
      }

      // Final compositing at level 0: the feather ring blends the vote with the original.
      Level& L0 = L[0];
      if ( P.feather > 0 )
      {
         Image voted = L0.img;
         Vote( nnf, L0, L0.synth, threads, &voted );
         const float inv = 1.0f / float( P.feather + 1 );
         for ( int c = 0; c < C; ++c )
         {
            float* I = L0.img.Plane( c );
            const float* Vt = voted.Plane( c );
            for ( std::size_t i = 0; i < L0.img.Pixels(); ++i )
            {
               if ( !L0.synth[i] )
                  continue;
               if ( L0.hole[i] )
                  I[i] = Vt[i];
               else
               {
                  float wgt = std::max( 0.0f, 1.0f - L0.holeDist[i] * inv );
                  I[i] = wgt * Vt[i] + ( 1.0f - wgt ) * I[i];
               }
            }
         }
      }

      if ( m_debug )
      {
         m_debugInfo.width = L0.w;
         m_debugInfo.height = L0.h;
         m_debugInfo.r = r;
         m_debugInfo.roiX0 = roi.x0;
         m_debugInfo.roiY0 = roi.y0;
         m_debugInfo.nnf = nnf;
         m_debugInfo.hole = L0.hole;
         m_debugInfo.target = L0.target;
         m_debugInfo.valid = L0.valid;
      }

      WriteBackRegion( L0.img, L0.synth, L0.hole, req.weight, roi, V );
      res.ok = true;
      report( "done" );
   }
   catch ( const std::bad_alloc& )
   {
      res.ok = false;
      res.error = "out of memory";
   }
   catch ( const std::exception& e )
   {
      res.ok = false;
      res.error = e.what();
   }
   return finish( res );
}

} // namespace repatch
