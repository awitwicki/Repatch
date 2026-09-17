#include "Stretch.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace repatch {

StretchParams ComputeAutoStretch( const float* plane, int w, int h, const Mask& exclude )
{
   const std::size_t n = std::size_t( w ) * h;
   const std::size_t stride = std::max<std::size_t>( 1, n / 1048576 );
   std::vector<float> samples;
   samples.reserve( n / stride + 1 );
   for ( std::size_t i = 0; i < n; i += stride )
   {
      if ( !exclude.empty() && exclude[i] )
         continue;
      float v = plane[i];
      if ( IsFiniteSample( v ) )
         samples.push_back( std::clamp( v, 0.0f, 1.0f ) );
   }

   StretchParams p;
   if ( samples.size() < 2 )
      return p;

   std::size_t mid = samples.size() / 2;
   std::nth_element( samples.begin(), samples.begin() + mid, samples.end() );
   float median = samples[mid];
   for ( float& v : samples )
      v = std::fabs( v - median );
   std::nth_element( samples.begin(), samples.begin() + mid, samples.end() );
   float mad = samples[mid];

   // No clipping when the data has no spread (MAD = 0); otherwise clip the
   // shadows 2.8 sigma below the median, as PixInsight's auto STF does.
   float sigma = 1.4826f * mad;
   p.c0 = ( mad > 0.0f ) ? std::clamp( median - 2.8f * sigma, 0.0f, 1.0f ) : 0.0f;
   if ( p.c0 >= 1.0f )
      return StretchParams{};

   float x0 = ( median - p.c0 ) / ( 1.0f - p.c0 );
   if ( x0 <= 0.0f || x0 >= 1.0f )
   {
      p.m = 0.5f;
      return p;
   }
   p.m = MTF( 0.25f, x0 ); // MTF(m, x0) = 0.25  <=>  m = MTF(0.25, x0)
   return p;
}

} // namespace repatch
