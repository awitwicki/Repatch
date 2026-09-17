#pragma once
// Minimal self-contained test harness. Cases register themselves with
// TEST_CASE; RunAll runs every case whose name starts with argv[1].
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace testing {

struct Case { const char* name; void ( *fn )(); };

inline std::vector<Case>& Registry() { static std::vector<Case> r; return r; }
inline int& Failures() { static int f = 0; return f; }

struct Registrar {
   Registrar( const char* n, void ( *f )() ) { Registry().push_back( { n, f } ); }
};

inline int RunAll( int argc, char** argv )
{
   const char* filter = argc > 1 ? argv[1] : "";
   int run = 0, failed = 0;
   for ( const Case& c : Registry() )
   {
      if ( std::strncmp( c.name, filter, std::strlen( filter ) ) != 0 )
         continue;
      int before = Failures();
      c.fn();
      ++run;
      bool ok = Failures() == before;
      if ( !ok )
         ++failed;
      std::printf( "%s %s\n", ok ? "PASS" : "FAIL", c.name );
   }
   std::printf( "%d case(s) run, %d failed\n", run, failed );
   return ( failed == 0 && run > 0 ) ? 0 : 1;
}

} // namespace testing

#define TEST_CASE( name ) \
   static void name(); \
   static testing::Registrar name##_registrar( #name, name ); \
   static void name()

#define CHECK( cond ) \
   do { if ( !( cond ) ) { std::printf( "  CHECK failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__ ); ++testing::Failures(); } } while ( 0 )

#define REQUIRE( cond ) \
   do { if ( !( cond ) ) { std::printf( "  REQUIRE failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__ ); ++testing::Failures(); return; } } while ( 0 )

#define CHECK_NEAR( a, b, eps ) \
   do { double _a = double( a ), _b = double( b ); \
        if ( !( std::fabs( _a - _b ) <= double( eps ) ) ) { \
           std::printf( "  CHECK_NEAR failed: %s = %g vs %s = %g (eps %g)  (%s:%d)\n", #a, _a, #b, _b, double( eps ), __FILE__, __LINE__ ); \
           ++testing::Failures(); } } while ( 0 )
