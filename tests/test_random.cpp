#include "testing.h"
#include "repatch/Random.h"

TEST_CASE( random_same_seed_same_sequence )
{
   repatch::Pcg32 a( 12345 ), b( 12345 );
   for ( int i = 0; i < 1000; ++i )
      CHECK( a.Next() == b.Next() );
}

TEST_CASE( random_different_seed_different_sequence )
{
   repatch::Pcg32 a( 1 ), b( 2 );
   int same = 0;
   for ( int i = 0; i < 100; ++i )
      same += ( a.Next() == b.Next() );
   CHECK( same < 5 );
}

TEST_CASE( random_range_is_inclusive_and_bounded )
{
   repatch::Pcg32 r( 7 );
   bool sawLo = false, sawHi = false;
   for ( int i = 0; i < 10000; ++i )
   {
      int v = r.Range( -3, 3 );
      CHECK( v >= -3 && v <= 3 );
      sawLo |= ( v == -3 );
      sawHi |= ( v == 3 );
   }
   CHECK( sawLo && sawHi );
   for ( int i = 0; i < 100; ++i )
      CHECK( r.Range( 5, 5 ) == 5 );
}

TEST_CASE( random_uniform_in_unit_interval )
{
   repatch::Pcg32 r( 99 );
   double sum = 0;
   for ( int i = 0; i < 100000; ++i )
   {
      float u = r.Uniform();
      CHECK( u >= 0.0f && u < 1.0f );
      sum += u;
   }
   CHECK_NEAR( sum / 100000, 0.5, 0.01 );
}

TEST_CASE( random_hashseed_varies_with_every_argument )
{
   uint64_t base = repatch::HashSeed( 1, 2, 3, 4 );
   CHECK( base != repatch::HashSeed( 2, 2, 3, 4 ) );
   CHECK( base != repatch::HashSeed( 1, 3, 3, 4 ) );
   CHECK( base != repatch::HashSeed( 1, 2, 4, 4 ) );
   CHECK( base != repatch::HashSeed( 1, 2, 3, 5 ) );
   CHECK( base == repatch::HashSeed( 1, 2, 3, 4 ) );
}

TEST_CASE( random_hashseed_no_known_collisions )
{
   // Regression test: an earlier bit-packing scheme collided on these inputs.
   CHECK( repatch::HashSeed( 0, 1u << 24, 0, 0 ) != repatch::HashSeed( 0, 0, 0, 0 ) );
   CHECK( repatch::HashSeed( 256, 0, 0, 0 ) != repatch::HashSeed( 0, 1, 0, 0 ) );
}
