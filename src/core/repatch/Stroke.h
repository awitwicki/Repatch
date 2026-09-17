#pragma once
// Brush strokes: weight profile, rasterization into a hole mask plus a
// compositing weight map, the region a stroke's fill depends on, per-stroke
// seeds, and FillStroke (one stroke filled in place).
#include <cstdint>
#include <vector>

#include "FillBackend.h"
#include "Mask.h"

namespace repatch {

struct StrokePoint {
   float x = 0;
   float y = 0;
};

// A brush stroke: a dense polyline of centres sharing one brush setting.
struct Stroke {
   std::vector<StrokePoint> points;
   int radius = 15;        // px, >= 1
   float softness = 0.5f;  // 0 = hard disc, 1 = fades over the whole radius
   float opacity = 1.0f;   // multiplies the compositing weight
};

// Weight of the brush at distance d from the stroke path: 1 up to
// radius*(1-softness), smoothstep down to 0 at radius, 0 beyond.
float BrushProfile( float d, int radius, float softness );

// Bounding box of the stroke's discs, clipped to [0,w)x[0,h). Empty when no
// disc touches the image.
BBox StrokeBounds( const Stroke& s, int w, int h );

// sampleRing > 0 ? sampleRing : max(32, 3*radius)
int EffectiveSampleRing( int sampleRing, int radius );

// Region a stroke's fill can depend on: StrokeBounds grown by
// feather + 2*(patchSize/2) + max(EffectiveSampleRing, searchRadius) + 1,
// clipped to the image. Matches PatchBackend's internal crop rule.
BBox StrokeRoi( const Stroke& s, int w, int h, const FillParams& params );

// Rasterizes the stroke over roi. mask/weight get roi.Width()*roi.Height()
// entries; distance is measured from pixel centres (x+0.5, y+0.5).
// mask[i] = (profile > 0); weight[i] = opacity * profile.
void RasterizeStroke( const Stroke& s, const BBox& roi, Mask& mask, std::vector<float>& weight );

// Non-zero seed for stroke number strokeIndex (0-based) of a session seed.
uint32_t DeriveStrokeSeed( uint32_t seed, int strokeIndex );

// Fills one stroke in place. The ROI is computed with StrokeRoi, the stroke
// is rasterized over it, and PatchBackend::Fill runs on a copy of the ROI
// with params.sampleRing replaced by EffectiveSampleRing. params.randomSeed
// is used as given (pass DeriveStrokeSeed). opacity == 0 is a successful
// no-op. Errors are reported like FillBackend::Fill.
FillResult FillStroke( ImageView image, const Stroke& s, const FillParams& params, const ProgressFn& progress = {} );

} // namespace repatch
