#pragma once
#include <cstddef>
#include <vector>

#include "Image.h"

namespace repatch {

// Half-open bounding box [x0,x1) x [y0,y1).
struct BBox {
   int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
   bool Empty() const { return x1 <= x0 || y1 <= y0; }
   int Width() const { return x1 - x0; }
   int Height() const { return y1 - y0; }
};

std::size_t CountSet( const Mask& m );
BBox MaskBBox( const Mask& m, int w, int h );

// Binary dilation with a (2r+1)x(2r+1) square (Chebyshev radius r).
Mask DilateChebyshev( const Mask& m, int w, int h, int r );

// Exact Euclidean distance (pixels) from every pixel to the nearest set pixel
// of m. 0 on set pixels; 1e10 everywhere if m is empty.
std::vector<float> DistanceTransform( const Mask& m, int w, int h, int threads );

// out[i] = dist[i] <= radius.
Mask ThresholdDistance( const std::vector<float>& dist, float radius );

// Valid source patch centres: patch fully inside the image, patch does not
// overlap `hole`, and (if sampleRing > 0) centre within max(sampleRing, r+1)
// pixels of the hole.
Mask ValidSourceMap( const Mask& hole, int w, int h, int r,
                     const std::vector<float>& holeDist, int sampleRing );

Mask CropMask( const Mask& m, int w, int h, const BBox& roi );

} // namespace repatch
