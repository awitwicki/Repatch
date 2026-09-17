#include "testing.h"
#include "Fits.h"

#include <cstdio>
#include <string>
#include <vector>

TEST_CASE( fits_roundtrip_mono_and_rgb )
{
   for ( int channels : { 1, 3 } )
   {
      repatch::Image img( 7, 5, channels );
      for ( size_t i = 0; i < img.data.size(); ++i )
         img.data[i] = float( i ) * 0.001f - 0.5f;
      std::string path = "test_tmp_roundtrip.fits", err;
      REQUIRE( repatch::fits::Write( path, img, err ) );
      repatch::Image back;
      REQUIRE( repatch::fits::Read( path, back, err ) );
      CHECK( back.width == 7 && back.height == 5 && back.channels == channels );
      CHECK( back.data == img.data );
      std::remove( path.c_str() );
   }
}

TEST_CASE( fits_reads_int16_with_bzero )
{
   // Hand-built BITPIX 16 file, 3x2, BZERO 32768: stored = value - 32768.
   std::string header;
   auto card = [&]( const std::string& k, const std::string& v ) {
      std::string s = k; s.resize( 8, ' ' ); s += "= ";
      std::string vv = v; vv.insert( 0, 20 - vv.size(), ' ' ); s += vv; s.resize( 80, ' ' ); header += s;
   };
   card( "SIMPLE", "T" ); card( "BITPIX", "16" ); card( "NAXIS", "2" ); card( "NAXIS1", "3" ); card( "NAXIS2", "2" );
   card( "BZERO", "32768" ); card( "BSCALE", "1" );
   std::string end = "END"; end.resize( 80, ' ' ); header += end;
   header.resize( 2880, ' ' );
   const int values[6] = { 0, 1, 65535, 32768, 100, 40000 };
   std::vector<unsigned char> data;
   for ( int v : values )
   {
      int stored = v - 32768; // int16
      unsigned short u = (unsigned short)( (short)stored );
      data.push_back( (unsigned char)( u >> 8 ) );
      data.push_back( (unsigned char)( u & 0xFF ) );
   }
   data.resize( 2880, 0 );
   std::string path = "test_tmp_int16.fits";
   FILE* f = std::fopen( path.c_str(), "wb" );
   REQUIRE( f != nullptr );
   std::fwrite( header.data(), 1, header.size(), f );
   std::fwrite( data.data(), 1, data.size(), f );
   std::fclose( f );

   repatch::Image img; std::string err;
   REQUIRE( repatch::fits::Read( path, img, err ) );
   CHECK( img.width == 3 && img.height == 2 && img.channels == 1 );
   for ( int i = 0; i < 6; ++i )
      CHECK_NEAR( img.data[i], float( values[i] ), 1e-3 );
   std::remove( path.c_str() );
}

TEST_CASE( fits_rejects_garbage_and_missing_files )
{
   std::string path = "test_tmp_garbage.fits", err;
   FILE* f = std::fopen( path.c_str(), "wb" );
   REQUIRE( f != nullptr );
   std::fputs( "hello, this is not a FITS file", f );
   std::fclose( f );
   repatch::Image img;
   CHECK( !repatch::fits::Read( path, img, err ) );
   CHECK( !err.empty() );
   std::remove( path.c_str() );
   err.clear();
   CHECK( !repatch::fits::Read( "definitely_missing_file.fits", img, err ) );
   CHECK( !err.empty() );
}
