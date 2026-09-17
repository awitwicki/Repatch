#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace repatch {

// Bit-pattern-based finiteness check for a 32-bit IEEE-754 float. Unlike
// std::isfinite(), this cannot be "optimized away" by -ffinite-math-only
// (implied by -ffast-math, which the PixInsight module build uses): it
// inspects the exponent bits directly instead of relying on the compiler's
// assumption that NaN/Inf never occur.
inline bool IsFiniteSample( float v )
{
   uint32_t u;
   std::memcpy( &u, &v, sizeof( u ) );
   return ( u & 0x7F800000u ) != 0x7F800000u;
}

// Non-owning view of a planar float image. Each channel is a separate
// contiguous width*height plane; planes need not be adjacent in memory
// (PixInsight allocates channels separately, so the module can pass its
// buffers without copying).
struct ImageView {
   int width = 0;
   int height = 0;
   int channels = 0;
   float* plane[3] = { nullptr, nullptr, nullptr };

   std::size_t Pixels() const { return std::size_t( width ) * height; }
};

// Owning planar float image used internally by the core.
struct Image {
   int width = 0;
   int height = 0;
   int channels = 0;
   std::vector<float> data; // channel-major: data[(c*height + y)*width + x]

   Image() = default;
   Image( int w, int h, int c )
      : width( w ), height( h ), channels( c ), data( std::size_t( w ) * h * c, 0.0f ) {}

   std::size_t Pixels() const { return std::size_t( width ) * height; }
   float* Plane( int c ) { return data.data() + std::size_t( c ) * Pixels(); }
   const float* Plane( int c ) const { return data.data() + std::size_t( c ) * Pixels(); }
   float& At( int c, int x, int y ) { return Plane( c )[std::size_t( y ) * width + x]; }
   float At( int c, int x, int y ) const { return Plane( c )[std::size_t( y ) * width + x]; }

   ImageView View()
   {
      ImageView v;
      v.width = width; v.height = height; v.channels = channels;
      for ( int c = 0; c < channels && c < 3; ++c )
         v.plane[c] = Plane( c );
      return v;
   }

   static Image FromView( const ImageView& v )
   {
      Image img( v.width, v.height, v.channels );
      for ( int c = 0; c < v.channels; ++c )
         std::copy( v.plane[c], v.plane[c] + v.Pixels(), img.Plane( c ) );
      return img;
   }
};

// One byte per pixel, nonzero = set. Row-major, width*height entries.
using Mask = std::vector<uint8_t>;

} // namespace repatch
