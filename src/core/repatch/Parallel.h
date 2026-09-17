#pragma once
// Row-band parallelism. Work is split into fixed-height bands whose layout
// depends only on the row count and a shift, never on the thread count, so
// any per-band computation is reproducible on any machine.
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

namespace repatch {

constexpr int kBandHeight = 16;

inline int ResolveThreads( int requested )
{
   if ( requested > 0 )
      return requested;
   unsigned hw = std::thread::hardware_concurrency();
   return hw == 0 ? 1 : int( hw );
}

// Band b covers rows [max(0, b*kBandHeight - shift), min(rows, (b+1)*kBandHeight - shift)).
inline int BandCount( int rows, int shift = 0 )
{
   return rows <= 0 ? 0 : ( rows + shift + kBandHeight - 1 ) / kBandHeight;
}

inline void BandRange( int rows, int shift, int band, int& y0, int& y1 )
{
   y0 = std::max( 0, band * kBandHeight - shift );
   y1 = std::min( rows, ( band + 1 ) * kBandHeight - shift );
}

using BandFn = std::function<void( int y0, int y1, int band )>;

inline void ParallelBands( int rows, int shift, int threads, const BandFn& fn )
{
   const int bands = BandCount( rows, shift );
   if ( bands == 0 )
      return;
   threads = std::max( 1, std::min( ResolveThreads( threads ), bands ) );

   auto work = [&]( int band ) {
      int y0, y1;
      BandRange( rows, shift, band, y0, y1 );
      if ( y0 < y1 )
         fn( y0, y1, band );
   };

   if ( threads == 1 )
   {
      // No thread boundary here, so an exception from `fn` propagates via
      // ordinary stack unwinding; no special handling is needed. The
      // try/catch/rethrow below is a no-op in practice but keeps this path
      // structurally symmetric with the multi-threaded one below.
      try
      {
         for ( int b = 0; b < bands; ++b )
            work( b );
      }
      catch ( ... )
      {
         throw;
      }
      return;
   }

   // Captures the first exception thrown by any band (from any thread,
   // including the calling thread's own worker() invocation below), so it
   // can be rethrown here instead of escaping a std::thread and calling
   // std::terminate(). Only one exception is captured: the atomic<bool>
   // gate ensures at most one thread ever writes to `firstException`, so no
   // further synchronization is needed for that write; all reads of it
   // happen only after every thread has been joined below, which is
   // already a happens-after relationship.
   std::atomic<bool> hasException{ false };
   std::exception_ptr firstException;
   std::atomic<int> next{ 0 };
   auto worker = [&]() {
      for ( ;; )
      {
         if ( hasException.load( std::memory_order_relaxed ) )
            break;
         int b = next.fetch_add( 1 );
         if ( b >= bands )
            break;
         try
         {
            work( b );
         }
         catch ( ... )
         {
            bool expected = false;
            if ( hasException.compare_exchange_strong( expected, true ) )
               firstException = std::current_exception();
            break;
         }
      }
   };
   std::vector<std::thread> pool;
   pool.reserve( std::size_t( threads ) - 1 );
   for ( int i = 1; i < threads; ++i )
      pool.emplace_back( worker );
   worker();
   for ( auto& t : pool )
      t.join();

   if ( firstException )
      std::rethrow_exception( firstException );
}

inline void ParallelRows( int rows, int threads, const std::function<void( int y )>& fn )
{
   ParallelBands( rows, 0, threads, [&]( int y0, int y1, int ) {
      for ( int y = y0; y < y1; ++y )
         fn( y );
   } );
}

} // namespace repatch
