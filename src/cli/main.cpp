// repatch-cli: runs the Repatch core on FITS images without PixInsight.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Fits.h"
#include "repatch/PatchBackend.h"
#include "repatch/Random.h"

namespace {

void Usage()
{
   std::fprintf( stderr,
      "repatch-cli: content-aware fill (PatchMatch) on FITS images\n"
      "\n"
      "  repatch-cli <in.fits> <mask.fits> <out.fits> [options]\n"
      "  repatch-cli --synth W H <image.fits> <mask.fits>\n"
      "\n"
      "Options (defaults match the PixInsight module):\n"
      "  --patch N       patch size, odd, 5..41 (11)\n"
      "  --levels N      pyramid levels, 0 = auto (0)\n"
      "  --iters N       PatchMatch passes per level (5)\n"
      "  --radius N      search radius in px, 0 = unbounded (0)\n"
      "  --ring N        sampling ring width in px, 0 = whole image (0)\n"
      "  --linear        match in linear space instead of stretched\n"
      "  --seed N        random seed, 0 = random (0)\n"
      "  --feather N     blend width at the hole boundary (3)\n"
      "  --threads N     worker threads, 0 = all cores (0)\n"
      "  --threshold F   mask value above which a pixel is a hole (0.5)\n"
      "\n"
      "--synth writes a deterministic test image (texture + stars + noise) and a\n"
      "rectangular hole mask covering the central fifth of each dimension.\n" );
}

int ParseInt( const char* s, const char* name )
{
   char* end = nullptr;
   long v = std::strtol( s, &end, 10 );
   if ( !end || *end != '\0' )
   {
      std::fprintf( stderr, "invalid value for %s: %s\n", name, s );
      std::exit( 2 );
   }
   return int( v );
}

bool WriteSynth( int w, int h, const std::string& imgPath, const std::string& maskPath, std::string& err )
{
   repatch::Image img( w, h, 1 );
   repatch::Pcg32 rng( 42 );
   for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
         img.At( 0, x, y ) = 0.2f + 0.1f * std::sin( 2 * 3.14159265f * x / 23 ) * std::cos( 2 * 3.14159265f * y / 17 );
   for ( int k = 0; k < 40; ++k )
   {
      int cx = rng.Range( 0, w - 1 ), cy = rng.Range( 0, h - 1 );
      float amp = 0.3f + 0.7f * rng.Uniform();
      float sigma = 1.0f + 2.0f * rng.Uniform();
      for ( int dy = -8; dy <= 8; ++dy )
         for ( int dx = -8; dx <= 8; ++dx )
         {
            int x = cx + dx, y = cy + dy;
            if ( x < 0 || y < 0 || x >= w || y >= h )
               continue;
            img.At( 0, x, y ) += amp * std::exp( -float( dx * dx + dy * dy ) / ( 2 * sigma * sigma ) );
         }
   }
   for ( float& v : img.data )
      v = std::min( 1.0f, v + 0.01f * ( rng.Uniform() - 0.5f ) );

   repatch::Image mask( w, h, 1 );
   for ( int y = h / 2 - h / 10; y < h / 2 + h / 10; ++y )
      for ( int x = w / 2 - w / 10; x < w / 2 + w / 10; ++x )
         mask.At( 0, x, y ) = 1.0f;

   return repatch::fits::Write( imgPath, img, err ) && repatch::fits::Write( maskPath, mask, err );
}

} // namespace

int main( int argc, char** argv )
{
   if ( argc >= 2 && std::strcmp( argv[1], "--synth" ) == 0 )
   {
      if ( argc != 6 )
      {
         Usage();
         return 2;
      }
      std::string err;
      if ( !WriteSynth( ParseInt( argv[2], "W" ), ParseInt( argv[3], "H" ), argv[4], argv[5], err ) )
      {
         std::fprintf( stderr, "error: %s\n", err.c_str() );
         return 1;
      }
      std::printf( "wrote %s and %s\n", argv[4], argv[5] );
      return 0;
   }
   if ( argc < 4 )
   {
      Usage();
      return 2;
   }

   repatch::FillParams p;
   float threshold = 0.5f;
   for ( int i = 4; i < argc; ++i )
   {
      std::string a = argv[i];
      auto next = [&]() -> const char* {
         if ( i + 1 >= argc )
         {
            std::fprintf( stderr, "missing value for %s\n", a.c_str() );
            std::exit( 2 );
         }
         return argv[++i];
      };
      if ( a == "--patch" )          p.patchSize = ParseInt( next(), "--patch" );
      else if ( a == "--levels" )    p.pyramidLevels = ParseInt( next(), "--levels" );
      else if ( a == "--iters" )     p.iterations = ParseInt( next(), "--iters" );
      else if ( a == "--radius" )    p.searchRadius = ParseInt( next(), "--radius" );
      else if ( a == "--ring" )      p.sampleRing = ParseInt( next(), "--ring" );
      else if ( a == "--linear" )    p.matchSpace = repatch::MatchSpace::Linear;
      else if ( a == "--seed" )      p.randomSeed = uint32_t( std::strtoul( next(), nullptr, 10 ) );
      else if ( a == "--feather" )   p.feather = ParseInt( next(), "--feather" );
      else if ( a == "--threads" )   p.threads = ParseInt( next(), "--threads" );
      else if ( a == "--threshold" ) threshold = float( std::atof( next() ) );
      else
      {
         std::fprintf( stderr, "unknown option: %s\n", a.c_str() );
         Usage();
         return 2;
      }
   }

   std::string err;
   repatch::Image img, mask;
   if ( !repatch::fits::Read( argv[1], img, err ) )
   {
      std::fprintf( stderr, "error: %s\n", err.c_str() );
      return 1;
   }
   if ( !repatch::fits::Read( argv[2], mask, err ) )
   {
      std::fprintf( stderr, "error: %s\n", err.c_str() );
      return 1;
   }
   if ( mask.width != img.width || mask.height != img.height )
   {
      std::fprintf( stderr, "error: mask dimensions (%dx%d) do not match the image (%dx%d)\n",
                    mask.width, mask.height, img.width, img.height );
      return 1;
   }

   std::vector<uint8_t> hole( img.Pixels() );
   repatch::ThresholdMask( mask.Plane( 0 ), img.width, img.height, threshold, hole.data() );

   repatch::FillRequest req;
   req.image = img.View();
   req.mask = hole.data();
   req.params = p;

   repatch::PatchBackend backend;
   repatch::FillResult res = backend.Fill( req, []( float f, const char* stage ) {
      std::printf( "\r%5.1f%%  %-32s", 100.0 * f, stage );
      std::fflush( stdout );
      return true;
   } );
   std::printf( "\n" );
   if ( !res.ok )
   {
      std::fprintf( stderr, "error: %s\n", res.error.c_str() );
      return 1;
   }
   if ( !repatch::fits::Write( argv[3], img, err ) )
   {
      std::fprintf( stderr, "error: %s\n", err.c_str() );
      return 1;
   }
   std::printf( "ok: %dx%dx%d, %d pyramid level(s), seed %u, %.2f s -> %s\n",
                img.width, img.height, img.channels, res.levelsUsed, unsigned( res.seedUsed ), res.seconds, argv[3] );
   return 0;
}
