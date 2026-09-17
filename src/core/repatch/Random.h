#pragma once
// Small deterministic PRNG (PCG32, Melissa O'Neill) plus a seed hash. All
// randomness in the core flows through these so results are reproducible.
#include <cstdint>

namespace repatch {

struct Pcg32 {
   uint64_t state = 0;
   uint64_t inc = 1;

   explicit Pcg32( uint64_t seed, uint64_t seq = 1 )
   {
      state = 0;
      inc = ( seq << 1 ) | 1u;
      Next();
      state += seed;
      Next();
   }

   uint32_t Next()
   {
      uint64_t old = state;
      state = old * 6364136223846793005ULL + inc;
      uint32_t xorshifted = uint32_t( ( ( old >> 18 ) ^ old ) >> 27 );
      uint32_t rot = uint32_t( old >> 59 );
      return ( xorshifted >> rot ) | ( xorshifted << ( ( 32 - rot ) & 31 ) );
   }

   // Uniform integer in [0, n). n must be > 0.
   uint32_t Below( uint32_t n ) { return uint32_t( ( uint64_t( Next() ) * n ) >> 32 ); }

   // Uniform integer in [lo, hi] (inclusive). Requires lo <= hi.
   int Range( int lo, int hi ) { return lo + int( Below( uint32_t( hi - lo + 1 ) ) ); }

   // Uniform float in [0, 1).
   float Uniform() { return float( Next() >> 8 ) * ( 1.0f / 16777216.0f ); }
};

// splitmix64-style mixing of a seed and three stream identifiers
// (e.g. level, pass, band) into a 64-bit seed. Uses sequential folding to
// ensure all input bits influence the output and avoid collisions.
inline uint64_t HashSeed( uint32_t seed, uint32_t a, uint32_t b, uint32_t c )
{
   uint64_t x = 0x9E3779B97F4A7C15ULL;
   for ( uint64_t v : { uint64_t( seed ), uint64_t( a ), uint64_t( b ), uint64_t( c ) } )
   {
      x += v + 0x9E3779B97F4A7C15ULL;
      x = ( x ^ ( x >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
      x = ( x ^ ( x >> 27 ) ) * 0x94D049BB133111EBULL;
      x ^= ( x >> 31 );
   }
   return x;
}

} // namespace repatch
