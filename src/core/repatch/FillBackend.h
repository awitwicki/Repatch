#pragma once
// Public API of the Repatch core. This is the only header the PixInsight
// module and the CLI need. No PCL dependency.
#include <cstdint>
#include <functional>
#include <string>

#include "Image.h"

namespace repatch {

enum class MatchSpace { Linear, Stretched };

struct FillParams {
   int patchSize = 11;          // odd, 5..41
   int pyramidLevels = 0;       // 0 = auto
   int iterations = 5;          // per level, 1..50
   int searchRadius = 0;        // 0 = unbounded (Chebyshev radius in px at level 0)
   int sampleRing = 0;          // 0 = whole image (Euclidean distance from hole, px)
   MatchSpace matchSpace = MatchSpace::Stretched;
   uint32_t randomSeed = 0;     // 0 = random; the seed actually used is reported
   int feather = 3;             // 0..256
   int threads = 0;             // 0 = hardware_concurrency
};

struct FillRequest {
   ImageView image;                 // in/out; channels must be 1 or 3
   const uint8_t* mask = nullptr;   // width*height, nonzero = hole
   // Optional compositing weights, width*height, values in [0,1] (clamped).
   // Read only for hole pixels: out = weight*fill + (1-weight)*original.
   // nullptr means weight 1 everywhere (plain replacement).
   const float* weight = nullptr;
   FillParams params;
};

// Called before each pass. Return false to abort.
using ProgressFn = std::function<bool( float fraction, const char* stage )>;

struct FillResult {
   bool ok = false;
   bool aborted = false;
   std::string error;
   int levelsUsed = 0;
   uint32_t seedUsed = 0;
   double seconds = 0;
};

class FillBackend {
public:
   virtual ~FillBackend() = default;
   virtual const char* Name() const = 0;
   // On success the request's image holds the filled result. On error or
   // abort the image is left exactly as it was.
   virtual FillResult Fill( FillRequest& request, const ProgressFn& progress = {} ) = 0;
};

// out[i] = (mono[i] > threshold) ? 1 : 0. Shared by the CLI and the module so
// both threshold identically.
void ThresholdMask( const float* mono, int w, int h, float threshold, uint8_t* out );

} // namespace repatch
