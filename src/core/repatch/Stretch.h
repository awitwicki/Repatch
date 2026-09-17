#pragma once
// PixInsight-style automatic screen stretch (shadows clip + midtones
// transfer function) used to build the matching image for linear data.
#include <cstddef>

#include "Image.h"

namespace repatch {

struct StretchParams {
   float c0 = 0.0f;   // shadows clipping point (highlights clip is always 1)
   float m = 0.5f;    // midtones balance; 0.5 = identity
};

// Midtones transfer function. Clips x to [0,1].
inline float MTF( float m, float x )
{
   if ( x <= 0.0f ) return 0.0f;
   if ( x >= 1.0f ) return 1.0f;
   if ( m == 0.5f ) return x;
   return ( m - 1.0f ) * x / ( ( 2.0f * m - 1.0f ) * x - m );
}

inline float ApplyStretch( const StretchParams& p, float x )
{
   float u = ( x - p.c0 ) / ( 1.0f - p.c0 );
   return MTF( p.m, u );
}

// Computes c0 = max(0, median - 2.8 * 1.4826 * MAD) (c0 = 0 when MAD = 0)
// and m such that the median maps to 0.25, from pixels where exclude[i] == 0
// (or all pixels if exclude is empty). Uses at most ~1M evenly strided
// samples. Returns identity params for degenerate inputs.
StretchParams ComputeAutoStretch( const float* plane, int w, int h, const Mask& exclude );

} // namespace repatch
