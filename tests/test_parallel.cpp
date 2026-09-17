#include "testing.h"
#include "repatch/Parallel.h"

#include <atomic>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

static void CheckCoverage( int rows, int shift, int threads )
{
   std::vector<std::atomic<int>> hits( rows );
   for ( auto& h : hits ) h = 0;
   repatch::ParallelBands( rows, shift, threads, [&]( int y0, int y1, int ) {
      for ( int y = y0; y < y1; ++y ) hits[y]++;
   } );
   for ( int y = 0; y < rows; ++y )
      CHECK( hits[y] == 1 );
}

TEST_CASE( parallel_bands_cover_every_row_exactly_once )
{
   for ( int rows : { 1, 7, 16, 17, 33, 100 } )
      for ( int shift : { 0, 8 } )
         for ( int threads : { 1, 3, 8 } )
            CheckCoverage( rows, shift, threads );
}

TEST_CASE( parallel_band_partition_independent_of_thread_count )
{
   // The set of (y0,y1,band) triples must be the same for 1 and 5 threads.
   auto collect = [&]( int threads ) {
      std::mutex m;
      std::vector<int> triples;
      repatch::ParallelBands( 100, 8, threads, [&]( int y0, int y1, int band ) {
         std::lock_guard<std::mutex> g( m );
         triples.push_back( band ); triples.push_back( y0 ); triples.push_back( y1 );
      } );
      // sort by band
      std::vector<int> sorted;
      for ( int b = 0; b < repatch::BandCount( 100, 8 ); ++b )
         for ( size_t i = 0; i < triples.size(); i += 3 )
            if ( triples[i] == b )
            { sorted.push_back( triples[i] ); sorted.push_back( triples[i+1] ); sorted.push_back( triples[i+2] ); }
      return sorted;
   };
   CHECK( collect( 1 ) == collect( 5 ) );
}

TEST_CASE( parallel_band_ranges_shift_by_half_band )
{
   int y0, y1;
   repatch::BandRange( 100, 0, 0, y0, y1 );
   CHECK( y0 == 0 && y1 == 16 );
   repatch::BandRange( 100, 8, 0, y0, y1 );
   CHECK( y0 == 0 && y1 == 8 );
   repatch::BandRange( 100, 8, 1, y0, y1 );
   CHECK( y0 == 8 && y1 == 24 );
   CHECK( repatch::BandCount( 100, 0 ) == 7 );
   CHECK( repatch::BandCount( 100, 8 ) == 7 );
   CHECK( repatch::BandCount( 0, 0 ) == 0 );
}

TEST_CASE( parallel_rows_matches_serial_sum )
{
   const int rows = 200;
   std::vector<long> perRow( rows, 0 );
   repatch::ParallelRows( rows, 6, [&]( int y ) { perRow[y] = long( y ) * y; } );
   long total = std::accumulate( perRow.begin(), perRow.end(), 0L );
   long expected = 0;
   for ( int y = 0; y < rows; ++y ) expected += long( y ) * y;
   CHECK( total == expected );
}

TEST_CASE( parallel_resolve_threads )
{
   CHECK( repatch::ResolveThreads( 4 ) == 4 );
   CHECK( repatch::ResolveThreads( 0 ) >= 1 );
   CHECK( repatch::ResolveThreads( -1 ) >= 1 );
}

TEST_CASE( parallel_bands_propagates_exception_after_joining_all_threads )
{
   const int rows = 4 * repatch::kBandHeight; // exactly 4 bands

   // Single-threaded path: work is strictly serial, so bands before the
   // throwing one are guaranteed to have run and the one after is not.
   {
      int ran = 0;
      bool threw = false;
      try
      {
         repatch::ParallelBands( rows, 0, 1, [&]( int, int, int band ) {
            if ( band == 2 )
               throw std::runtime_error( "band 2 boom" );
            ++ran;
         } );
      }
      catch ( const std::runtime_error& e )
      {
         threw = true;
         CHECK( std::string( e.what() ) == "band 2 boom" );
      }
      CHECK( threw );
      CHECK( ran == 2 ); // bands 0 and 1 ran before band 2 threw; band 3 never did
   }

   // Multi-threaded path: the exception must still propagate to the caller
   // rather than escaping a std::thread (which would call std::terminate()),
   // and every spawned thread must be joined -- if a join() were skipped on
   // the throw path, this call itself would hang or crash the test binary.
   {
      bool threw = false;
      try
      {
         repatch::ParallelBands( rows, 0, 4, [&]( int, int, int band ) {
            if ( band == 2 )
               throw std::runtime_error( "band 2 boom" );
         } );
      }
      catch ( const std::runtime_error& e )
      {
         threw = true;
         CHECK( std::string( e.what() ) == "band 2 boom" );
      }
      CHECK( threw );
   }
}
