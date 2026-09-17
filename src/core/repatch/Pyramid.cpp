#include "Pyramid.h"

#include <algorithm>
#include <cmath>

#include "Parallel.h"

namespace repatch {

Image DownsampleImage( const Image& src, int threads )
{
   const int w = src.width, h = src.height;
   const int W = HalfSize( w ), H = HalfSize( h );
   Image dst( W, H, src.channels );
   static const float k[3] = { 0.25f, 0.5f, 0.25f };
   for ( int c = 0; c < src.channels; ++c )
   {
      const float* S = src.Plane( c );
      float* D = dst.Plane( c );
      ParallelRows( H, threads, [&]( int Y ) {
         for ( int X = 0; X < W; ++X )
         {
            float acc = 0;
            for ( int j = -1; j <= 1; ++j )
            {
               int y = std::clamp( 2 * Y + j, 0, h - 1 );
               for ( int i = -1; i <= 1; ++i )
               {
                  int x = std::clamp( 2 * X + i, 0, w - 1 );
                  acc += k[j + 1] * k[i + 1] * S[std::size_t( y ) * w + x];
               }
            }
            D[std::size_t( Y ) * W + X] = acc;
         }
      } );
   }
   return dst;
}

Mask DownsampleMask( const Mask& m, int w, int h )
{
   const int W = HalfSize( w ), H = HalfSize( h );
   Mask out( std::size_t( W ) * H, 0 );
   for ( int Y = 0; Y < H; ++Y )
      for ( int X = 0; X < W; ++X )
      {
         uint8_t any = 0;
         for ( int j = -1; j <= 1 && !any; ++j )
         {
            int y = std::clamp( 2 * Y + j, 0, h - 1 );
            for ( int i = -1; i <= 1 && !any; ++i )
            {
               int x = std::clamp( 2 * X + i, 0, w - 1 );
               any |= ( m[std::size_t( y ) * w + x] != 0 );
            }
         }
         out[std::size_t( Y ) * W + X] = any;
      }
   return out;
}

int AutoPyramidLevels( const BBox& holeBBox, int patchSize )
{
   int m = std::min( holeBBox.Width(), holeBBox.Height() );
   double v = double( m ) / double( std::max( 1, patchSize ) );
   int L = ( v < 1.0 ) ? 1 : 1 + int( std::floor( std::log2( v ) ) );
   return std::clamp( L, 1, 8 );
}

int CapPyramidLevels( int levels, int w, int h, int patchSize )
{
   levels = std::max( 1, levels );
   for ( ;; )
   {
      int cw = w, ch = h;
      for ( int k = 1; k < levels; ++k )
      {
         cw = HalfSize( cw );
         ch = HalfSize( ch );
      }
      if ( levels == 1 || ( cw >= 2 * patchSize && ch >= 2 * patchSize ) )
         return levels;
      --levels;
   }
}

} // namespace repatch
