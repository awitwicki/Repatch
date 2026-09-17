#include "Fits.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace repatch::fits {

namespace {

constexpr std::size_t kBlock = 2880;

struct Header {
   bool simple = false;
   int bitpix = 0;
   int naxis = 0;
   long naxes[3] = { 0, 0, 0 };
   double bzero = 0.0;
   double bscale = 1.0;
};

std::string Trim( const std::string& s )
{
   std::size_t a = s.find_first_not_of( " \t" );
   if ( a == std::string::npos )
      return std::string();
   std::size_t b = s.find_last_not_of( " \t" );
   return s.substr( a, b - a + 1 );
}

bool ParseHeader( FILE* f, Header& h, std::string& err )
{
   std::vector<char> block( kBlock );
   for ( ;; )
   {
      if ( std::fread( block.data(), 1, kBlock, f ) != kBlock )
      {
         err = "truncated or invalid FITS header";
         return false;
      }
      for ( std::size_t c = 0; c < kBlock; c += 80 )
      {
         std::string card( block.data() + c, 80 );
         std::string key = Trim( card.substr( 0, 8 ) );
         if ( key == "END" )
            return true;
         if ( card[8] != '=' )
            continue;
         std::string val = card.substr( 10 );
         std::size_t slash = val.find( '/' );
         if ( slash != std::string::npos )
            val = val.substr( 0, slash );
         val = Trim( val );
         if ( key == "SIMPLE" )      h.simple = ( val == "T" );
         else if ( key == "BITPIX" ) h.bitpix = std::atoi( val.c_str() );
         else if ( key == "NAXIS" )  h.naxis = std::atoi( val.c_str() );
         else if ( key == "NAXIS1" ) h.naxes[0] = std::atol( val.c_str() );
         else if ( key == "NAXIS2" ) h.naxes[1] = std::atol( val.c_str() );
         else if ( key == "NAXIS3" ) h.naxes[2] = std::atol( val.c_str() );
         else if ( key == "BZERO" )  h.bzero = std::atof( val.c_str() );
         else if ( key == "BSCALE" ) h.bscale = std::atof( val.c_str() );
      }
   }
}

} // namespace

bool Read( const std::string& path, Image& out, std::string& err )
{
   FILE* f = std::fopen( path.c_str(), "rb" );
   if ( !f )
   {
      err = "cannot open " + path;
      return false;
   }
   Header h;
   if ( !ParseHeader( f, h, err ) )
   {
      std::fclose( f );
      return false;
   }
   auto failWith = [&]( const char* m ) {
      err = m;
      std::fclose( f );
      return false;
   };
   if ( !h.simple )
      return failWith( "not a FITS file (SIMPLE != T)" );
   if ( h.naxis != 2 && h.naxis != 3 )
      return failWith( "only NAXIS = 2 or 3 is supported" );
   const long w = h.naxes[0], hh = h.naxes[1], c = ( h.naxis == 3 ) ? h.naxes[2] : 1;
   if ( w <= 0 || hh <= 0 )
      return failWith( "invalid image dimensions" );
   if ( c != 1 && c != 3 )
      return failWith( "NAXIS3 must be 1 or 3" );
   if ( h.bitpix != 16 && h.bitpix != -32 && h.bitpix != -64 )
      return failWith( "unsupported BITPIX (need 16, -32 or -64)" );

   const std::size_t n = std::size_t( w ) * hh * c;
   const int bytes = std::abs( h.bitpix ) / 8;
   std::vector<unsigned char> raw( n * bytes );
   if ( std::fread( raw.data(), 1, raw.size(), f ) != raw.size() )
      return failWith( "truncated FITS data" );
   std::fclose( f );

   out = Image( int( w ), int( hh ), int( c ) );
   const unsigned char* p = raw.data();
   for ( std::size_t i = 0; i < n; ++i, p += bytes )
   {
      double v;
      if ( h.bitpix == 16 )
      {
         int16_t s = int16_t( ( uint16_t( p[0] ) << 8 ) | p[1] );
         v = double( s );
      }
      else if ( h.bitpix == -32 )
      {
         uint32_t u = ( uint32_t( p[0] ) << 24 ) | ( uint32_t( p[1] ) << 16 ) | ( uint32_t( p[2] ) << 8 ) | p[3];
         float fv;
         std::memcpy( &fv, &u, 4 );
         v = fv;
      }
      else
      {
         uint64_t u = 0;
         for ( int k = 0; k < 8; ++k )
            u = ( u << 8 ) | p[k];
         double dv;
         std::memcpy( &dv, &u, 8 );
         v = dv;
      }
      out.data[i] = float( h.bscale * v + h.bzero );
   }
   return true;
}

bool Write( const std::string& path, const Image& img, std::string& err )
{
   std::string header;
   auto card = [&]( const char* key, const std::string& value, const char* comment ) {
      std::string s( key );
      s.resize( 8, ' ' );
      s += "= ";
      std::string v = value;
      if ( v.size() < 20 )
         v.insert( 0, 20 - v.size(), ' ' );
      s += v;
      if ( comment && *comment )
      {
         s += " / ";
         s += comment;
      }
      s.resize( 80, ' ' );
      header += s;
   };
   card( "SIMPLE", "T", "conforms to FITS standard" );
   card( "BITPIX", "-32", "32-bit IEEE float" );
   card( "NAXIS", ( img.channels == 1 ) ? "2" : "3", "" );
   card( "NAXIS1", std::to_string( img.width ), "" );
   card( "NAXIS2", std::to_string( img.height ), "" );
   if ( img.channels != 1 )
      card( "NAXIS3", std::to_string( img.channels ), "" );
   card( "BZERO", "0", "" );
   card( "BSCALE", "1", "" );
   {
      std::string e = "END";
      e.resize( 80, ' ' );
      header += e;
   }
   header.resize( ( ( header.size() + kBlock - 1 ) / kBlock ) * kBlock, ' ' );

   FILE* f = std::fopen( path.c_str(), "wb" );
   if ( !f )
   {
      err = "cannot create " + path;
      return false;
   }
   bool ok = std::fwrite( header.data(), 1, header.size(), f ) == header.size();
   std::vector<unsigned char> raw( img.data.size() * 4 );
   for ( std::size_t i = 0; i < img.data.size(); ++i )
   {
      uint32_t u;
      std::memcpy( &u, &img.data[i], 4 );
      raw[4 * i + 0] = (unsigned char)( u >> 24 );
      raw[4 * i + 1] = (unsigned char)( ( u >> 16 ) & 0xFF );
      raw[4 * i + 2] = (unsigned char)( ( u >> 8 ) & 0xFF );
      raw[4 * i + 3] = (unsigned char)( u & 0xFF );
   }
   ok = ok && std::fwrite( raw.data(), 1, raw.size(), f ) == raw.size();
   std::size_t pad = ( kBlock - raw.size() % kBlock ) % kBlock;
   if ( pad )
   {
      std::vector<char> zeros( pad, 0 );
      ok = ok && std::fwrite( zeros.data(), 1, pad, f ) == pad;
   }
   ok = ( std::fclose( f ) == 0 ) && ok;
   if ( !ok )
      err = "write error on " + path;
   return ok;
}

} // namespace repatch::fits
