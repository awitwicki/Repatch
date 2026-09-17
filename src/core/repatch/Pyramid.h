#pragma once
#include "Image.h"
#include "Mask.h"

namespace repatch {

inline int HalfSize( int n ) { return ( n + 1 ) / 2; }

// 3x3 binomial filter ([1 2 1]/4 per axis, edge-clamped) then decimation by 2.
// Coarse pixel X covers fine pixels 2X-1..2X+1 (centre weight 1/2).
Image DownsampleImage( const Image& src, int threads );

// Coarse pixel is set if ANY fine pixel in its 3x3 support is set.
Mask DownsampleMask( const Mask& m, int w, int h );

// clamp(1 + floor(log2(min(bboxW, bboxH) / patchSize)), 1, 8)
int AutoPyramidLevels( const BBox& holeBBox, int patchSize );

// Reduces `levels` until the coarsest level is at least 2*patchSize on both
// sides; never below 1.
int CapPyramidLevels( int levels, int w, int h, int patchSize );

} // namespace repatch
