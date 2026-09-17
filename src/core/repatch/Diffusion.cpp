#include "Diffusion.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Parallel.h"

namespace repatch {

int DiffusionFill( Image& img, const Mask& hole, int threads, int maxSweeps, float relTol )
{
   const int w = img.width, h = img.height, C = img.channels;
   BBox bb = MaskBBox( hole, w, h );
   if ( bb.Empty() )
      return 0;

   // Per-channel range of known pixels (for the tolerance) and mean (initial guess).
   std::vector<float> range( C, 1.0f );
   for ( int c = 0; c < C; ++c )
   {
      float* P = img.Plane( c );
      double sum = 0; std::size_t n = 0;
      float lo = 0, hi = 0; bool first = true;
      for ( std::size_t i = 0; i < img.Pixels(); ++i )
         if ( !hole[i] )
         {
            float v = P[i];
            sum += v; ++n;
            if ( first ) { lo = hi = v; first = false; }
            else { lo = std::min( lo, v ); hi = std::max( hi, v ); }
         }
      float mean = n ? float( sum / n ) : 0.0f;
      range[c] = ( hi > lo ) ? ( hi - lo ) : 1.0f;
      for ( std::size_t i = 0; i < img.Pixels(); ++i )
         if ( hole[i] )
            P[i] = mean;
   }

   const float omega = 1.8f;
   const int rows = bb.Height();
   std::vector<float> rowMax( rows );

   int sweep = 0;
   for ( ; sweep < maxSweeps; ++sweep )
   {
      bool converged = true;
      for ( int c = 0; c < C; ++c )
      {
         float* P = img.Plane( c );
         float channelMax = 0;
         for ( int parity = 0; parity < 2; ++parity )
         {
            ParallelRows( rows, threads, [&]( int ry ) {
               int y = bb.y0 + ry;
               float localMax = 0;
               for ( int x = bb.x0; x < bb.x1; ++x )
               {
                  if ( ( ( x + y ) & 1 ) != parity )
                     continue;
                  std::size_t i = std::size_t( y ) * w + x;
                  if ( !hole[i] )
                     continue;
                  float sum = 0; int n = 0;
                  if ( x > 0 )     { sum += P[i - 1]; ++n; }
                  if ( x < w - 1 ) { sum += P[i + 1]; ++n; }
                  if ( y > 0 )     { sum += P[i - w]; ++n; }
                  if ( y < h - 1 ) { sum += P[i + w]; ++n; }
                  if ( n == 0 )
                     continue;
                  float delta = omega * ( sum / n - P[i] );
                  P[i] += delta;
                  localMax = std::max( localMax, std::fabs( delta ) );
               }
               rowMax[ry] = std::max( parity == 0 ? 0.0f : rowMax[ry], localMax );
            } );
         }
         for ( int ry = 0; ry < rows; ++ry )
            channelMax = std::max( channelMax, rowMax[ry] );
         if ( channelMax > relTol * range[c] )
            converged = false;
      }
      if ( converged )
      {
         ++sweep;
         break;
      }
   }
   return sweep;
}

} // namespace repatch
