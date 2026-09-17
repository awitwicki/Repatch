#pragma once
// Nearest-neighbour field (NNF) machinery for one pyramid level:
// patch distance, random/propagated/upsampled candidates, and voting.
#include <cstdint>
#include <string>
#include <vector>

#include "Image.h"
#include "Mask.h"
#include "Random.h"
#include "Stretch.h"

namespace repatch {

constexpr float kInfDistance = 3.0e38f;

// Per-pixel best source centre and its patch distance. Entries are only
// meaningful where Level::target is set.
struct Nnf {
   int w = 0, h = 0;
   std::vector<int32_t> sx, sy;
   std::vector<float> d;

   void Resize( int w_, int h_ )
   {
      w = w_; h = h_;
      sx.assign( std::size_t( w ) * h, 0 );
      sy.assign( std::size_t( w ) * h, 0 );
      d.assign( std::size_t( w ) * h, kInfDistance );
   }
};

// Everything the algorithm needs for one pyramid level.
struct Level {
   int w = 0, h = 0;
   int r = 0;                    // patch radius (patchSize / 2)
   int searchRadius = 0;         // 0 = unbounded
   Image img;                    // linear pixels; hole pixels hold the current estimate
   Image match;                  // image used for matching (stretched, or a copy of img)
   Mask hole;                    // H
   Mask synth;                   // S: H dilated by feather (level 0), == H elsewhere
   Mask target;                  // T: S dilated by r (Chebyshev)
   Mask valid;                   // V: valid source patch centres
   std::vector<float> holeDist;  // Euclidean distance to H
   std::vector<int32_t> validList; // linear indices of all valid pixels
   const StretchParams* stretch = nullptr; // per-channel params, or nullptr for Linear

   // Recomputes `match` from `img` on the pixels of `region` (all pixels if null).
   void RefreshMatch( const Mask* region, int threads );
};

// Computes holeDist, synth, target, valid, validList and match. Requires img,
// hole, r, searchRadius and stretch to be set. Returns false with a message
// when there is no usable source region.
bool BuildLevelRegions( Level& L, int feather, int sampleRing, int threads, std::string& err );

// Sum of squared differences over all channels of L.match between the patch
// centred at (tx,ty) and the one at (sx,sy). Target pixels outside the image
// are skipped; the source patch must be fully inside. Returns a value > best
// as soon as the running sum exceeds best.
float PatchDistance( const Level& L, int tx, int ty, int sx, int sy, float best );

// Source centre inside the image, in V, and within searchRadius of the target.
bool IsCandidateValid( const Level& L, int tx, int ty, int sx, int sy );

// Draws a uniformly random valid source for (tx,ty). Returns false only if no
// valid source exists within the search radius.
bool RandomValidSource( const Level& L, Pcg32& rng, int tx, int ty, int& sx, int& sy );

void InitNnfRandom( Nnf& nnf, const Level& L, int levelIndex, uint32_t seed, int threads );
void RefreshDistances( Nnf& nnf, const Level& L, int threads );

// One propagation + random-search pass. Even passes scan forward, odd passes
// backward with band boundaries shifted by half a band. Calls
// RefreshDistances first (the vote changed the target contents).
void PatchMatchPass( Nnf& nnf, const Level& L, int levelIndex, int pass, uint32_t seed, int threads );

// Reconstructs every pixel of `region` as the mean of the source pixels
// proposed by all target patches covering it. With out == nullptr the result
// is written into L.img (region must be a subset of L.hole so reads and
// writes never overlap) and L.match is refreshed there; otherwise it is
// written into *out (same geometry as L.img) and L is left unchanged.
// (In this codebase, every in-place call site passes region == L.hole
// exactly; a strict, non-trivial subset is safe by the invariant above but
// is not currently exercised by any caller or test.)
void Vote( const Nnf& nnf, Level& L, const Mask& region, int threads, Image* out = nullptr );

// Builds the fine level's NNF from the coarse one (coordinates and offsets
// doubled), falling back to random valid sources where the mapping is invalid.
void UpsampleNnf( const Nnf& coarse, const Level& coarseL, Nnf& fine, const Level& fineL,
                  int levelIndex, uint32_t seed, int threads );

} // namespace repatch
