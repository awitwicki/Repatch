#include "RepatchInstance.h"
#include "RepatchInterface.h"
#include "RepatchParameters.h"

#include <pcl/AutoViewLock.h>
#include <pcl/Console.h>
#include <pcl/ImageWindow.h>
#include <pcl/MetaModule.h>
#include <pcl/StandardStatus.h>
#include <pcl/Thread.h>
#include <pcl/Utility.h> // Range()
#include <pcl/View.h>

#include <random>

#include "repatch/PatchBackend.h"

namespace pcl
{

// ----------------------------------------------------------------------------

RepatchInstance::RepatchInstance( const MetaProcess* m )
   : ProcessImplementation( m )
   , p_mode( RPMode::Default )
   , p_maskId( TheRPMaskIdParameter->DefaultValue() )
   , p_maskThreshold( float( TheRPMaskThresholdParameter->DefaultValue() ) )
   , p_patchSize( int32( TheRPPatchSizeParameter->DefaultValue() ) )
   , p_pyramidLevelsAuto( TheRPPyramidLevelsAutoParameter->DefaultValue() )
   , p_pyramidLevels( int32( TheRPPyramidLevelsParameter->DefaultValue() ) )
   , p_iterations( int32( TheRPIterationsParameter->DefaultValue() ) )
   , p_searchRadius( int32( TheRPSearchRadiusParameter->DefaultValue() ) )
   , p_sampleRing( int32( TheRPSampleRingParameter->DefaultValue() ) )
   , p_matchSpace( RPMatchSpace::Default )
   , p_randomSeed( uint32( TheRPRandomSeedParameter->DefaultValue() ) )
   , p_feather( int32( TheRPFeatherParameter->DefaultValue() ) )
   , p_maskSource( RPMaskSource::Default )
   , p_brushRadius( int32( TheRPBrushRadiusParameter->DefaultValue() ) )
   , p_brushSoftness( float( TheRPBrushSoftnessParameter->DefaultValue() ) )
   , p_brushOpacity( float( TheRPBrushOpacityParameter->DefaultValue() ) )
{
}

RepatchInstance::RepatchInstance( const RepatchInstance& x )
   : ProcessImplementation( x )
{
   Assign( x );
}

void RepatchInstance::Assign( const ProcessImplementation& p )
{
   const RepatchInstance* x = dynamic_cast<const RepatchInstance*>( &p );
   if ( x != nullptr )
   {
      p_mode              = x->p_mode;
      p_maskId            = x->p_maskId;
      p_maskThreshold     = x->p_maskThreshold;
      p_patchSize         = x->p_patchSize;
      p_pyramidLevelsAuto = x->p_pyramidLevelsAuto;
      p_pyramidLevels     = x->p_pyramidLevels;
      p_iterations        = x->p_iterations;
      p_searchRadius      = x->p_searchRadius;
      p_sampleRing        = x->p_sampleRing;
      p_matchSpace        = x->p_matchSpace;
      p_randomSeed        = x->p_randomSeed;
      p_feather           = x->p_feather;
      p_maskSource        = x->p_maskSource;
      p_brushRadius       = x->p_brushRadius;
      p_brushSoftness     = x->p_brushSoftness;
      p_brushOpacity      = x->p_brushOpacity;
      p_strokes           = x->p_strokes;
   }
   isInterfaceInstance = false;
}

UndoFlags RepatchInstance::UndoMode( const View& ) const
{
   return UndoFlag::PixelData;
}

bool RepatchInstance::CanExecuteOn( const View& view, String& whyNot ) const
{
   if ( view.Image().IsComplexSample() )
   {
      whyNot = "Repatch cannot be executed on complex images.";
      return false;
   }
   if ( p_maskSource == RPMaskSource::Brush )
   {
      if ( p_strokes.IsEmpty() )
      {
         whyNot = "No brush strokes to apply. Paint on the image first.";
         return false;
      }
      return true;
   }
   if ( p_maskId.IsEmpty() )
   {
      whyNot = "No mask image has been specified.";
      return false;
   }
   View maskView = View::ViewById( IsoString( p_maskId ) );
   if ( maskView.IsNull() )
   {
      whyNot = "No such mask view: " + p_maskId;
      return false;
   }
   if ( maskView.FullId() == view.FullId() )
   {
      whyNot = "The mask image cannot be the target image.";
      return false;
   }
   return true;
}

// ----------------------------------------------------------------------------

static void ConvertToFloat( const ImageVariant& v, Image& out )
{
   if ( v.IsFloatSample() )
      switch ( v.BitsPerSample() )
      {
      case 32: out.Assign( static_cast<const Image&>( *v ) ); break;
      case 64: out.Assign( static_cast<const DImage&>( *v ) ); break;
      default: throw Error( "Unsupported image sample type." );
      }
   else
      switch ( v.BitsPerSample() )
      {
      case  8: out.Assign( static_cast<const UInt8Image&>( *v ) ); break;
      case 16: out.Assign( static_cast<const UInt16Image&>( *v ) ); break;
      case 32: out.Assign( static_cast<const UInt32Image&>( *v ) ); break;
      default: throw Error( "Unsupported image sample type." );
      }
}

static void ConvertFromFloat( const Image& in, ImageVariant& v )
{
   if ( v.IsFloatSample() )
      switch ( v.BitsPerSample() )
      {
      case 32: static_cast<Image&>( *v ).Assign( in ); break;
      case 64: static_cast<DImage&>( *v ).Assign( in ); break;
      default: throw Error( "Unsupported image sample type." );
      }
   else
      switch ( v.BitsPerSample() )
      {
      case  8: static_cast<UInt8Image&>( *v ).Assign( in ); break;
      case 16: static_cast<UInt16Image&>( *v ).Assign( in ); break;
      case 32: static_cast<UInt32Image&>( *v ).Assign( in ); break;
      default: throw Error( "Unsupported image sample type." );
      }
}

template <class P>
static void PasteTileImpl( GenericImage<P>& image, const GenericImage<P>& tile )
{
   image.Apply( tile );
}

ImageVariant RepatchInstance::CopyTile( const ImageVariant& image, const Rect& r )
{
   ImageVariant tile;
   image.ResetSelections();
   image.SelectRectangle( r );
   tile.CopyImage( image );
   image.ResetSelections();
   return tile;
}

void RepatchInstance::PasteTile( ImageVariant& image, const Rect& at, const ImageVariant& tile )
{
   image.ResetSelections();
   image.SelectPoint( at.x0, at.y0 );
   if ( image.IsFloatSample() )
      switch ( image.BitsPerSample() )
      {
      case 32: PasteTileImpl( static_cast<Image&>( *image ), static_cast<const Image&>( *tile ) ); break;
      case 64: PasteTileImpl( static_cast<DImage&>( *image ), static_cast<const DImage&>( *tile ) ); break;
      default: throw Error( "Unsupported image sample type." );
      }
   else
      switch ( image.BitsPerSample() )
      {
      case  8: PasteTileImpl( static_cast<UInt8Image&>( *image ), static_cast<const UInt8Image&>( *tile ) ); break;
      case 16: PasteTileImpl( static_cast<UInt16Image&>( *image ), static_cast<const UInt16Image&>( *tile ) ); break;
      case 32: PasteTileImpl( static_cast<UInt32Image&>( *image ), static_cast<const UInt32Image&>( *tile ) ); break;
      default: throw Error( "Unsupported image sample type." );
      }
   image.ResetSelections();
}

repatch::Stroke RepatchInstance::StrokeFromRows( const StrokeRow* rows, size_type count )
{
   repatch::Stroke s;
   if ( count == 0 )
      return s;
   s.radius = rows[0].radius;
   s.softness = rows[0].softness;
   s.opacity = rows[0].opacity;
   s.points.reserve( count );
   for ( size_type i = 0; i < count; ++i )
      s.points.push_back( { rows[i].x, rows[i].y } );
   return s;
}

repatch::FillParams RepatchInstance::CoreParams() const
{
   repatch::FillParams p;
   p.patchSize = p_patchSize;
   p.pyramidLevels = p_pyramidLevelsAuto ? 0 : p_pyramidLevels;
   p.iterations = p_iterations;
   p.searchRadius = p_searchRadius;
   p.sampleRing = p_sampleRing;
   p.matchSpace = ( p_matchSpace == RPMatchSpace::Linear ) ? repatch::MatchSpace::Linear
                                                             : repatch::MatchSpace::Stretched;
   p.randomSeed = p_randomSeed;
   p.feather = p_feather;
   p.threads = Thread::NumberOfThreads( 1024, 1 );
   return p;
}

Rect RepatchInstance::FillStroke( ImageVariant& image, const repatch::Stroke& stroke, uint32 strokeSeed ) const
{
   if ( image.IsComplexSample() )
      throw Error( "Repatch cannot be executed on complex images." );

   repatch::FillParams params = CoreParams();
   params.randomSeed = strokeSeed;
   params.sampleRing = repatch::EffectiveSampleRing( params.sampleRing, stroke.radius );

   const repatch::BBox roi = repatch::StrokeRoi( stroke, image.Width(), image.Height(), params );
   if ( roi.Empty() )
      throw Error( "The brush stroke lies outside the image." );
   const Rect r( roi.x0, roi.y0, roi.x1, roi.y1 );

   ImageVariant tile = CopyTile( image, r );
   Image work;
   ConvertToFloat( tile, work );
   const int channels = work.NumberOfNominalChannels();
   if ( channels != 1 && channels != 3 )
      throw Error( String().Format( "Unsupported number of nominal channels: %d", channels ) );

   repatch::Stroke local = stroke;
   for ( repatch::StrokePoint& q : local.points )
   {
      q.x -= float( roi.x0 );
      q.y -= float( roi.y0 );
   }

   repatch::ImageView v;
   v.width = r.Width();
   v.height = r.Height();
   v.channels = channels;
   for ( int c = 0; c < channels; ++c )
      v.plane[c] = work.PixelData( c );

   repatch::FillResult res = repatch::FillStroke( v, local, params );
   if ( !res.ok )
      throw Error( "Repatch: " + String( res.error.c_str() ) );

   ConvertFromFloat( work, tile );
   PasteTile( image, r, tile );
   return r;
}

// ----------------------------------------------------------------------------

std::vector<uint8_t> RepatchInstance::BuildHoleMask( const View& target ) const
{
   if ( p_maskId.IsEmpty() )
      throw Error( "No mask image has been specified." );

   View maskView = View::ViewById( IsoString( p_maskId ) );
   if ( maskView.IsNull() )
      throw Error( "No such mask view: " + p_maskId );
   if ( maskView.FullId() == target.FullId() )
      throw Error( "The mask image cannot be the target image." );

   ImageVariant maskVariant = maskView.Image();
   if ( maskVariant.IsComplexSample() )
      throw Error( "The mask image cannot be a complex-valued image." );

   Image maskImage;
   ConvertToFloat( maskVariant, maskImage );
   if ( maskImage.NumberOfNominalChannels() > 1 )
      Console().WarningLn( "<end><cbr>** Warning: The mask image has several channels; using channel 0 only." );

   const int tw = target.Width(), th = target.Height();
   if ( maskImage.Width() != tw || maskImage.Height() != th )
   {
      bool cropped = false;
      if ( target.IsPreview() )
      {
         View mainView = target.Window().MainView();
         if ( maskImage.Width() == mainView.Width() && maskImage.Height() == mainView.Height() )
         {
            maskImage.CropTo( target.Window().PreviewRect( target.Id() ) );
            cropped = true;
         }
      }
      if ( !cropped )
         throw Error( String().Format( "Mask dimensions (%dx%d) do not match the target image (%dx%d).",
                                       maskImage.Width(), maskImage.Height(), tw, th ) );
      if ( maskImage.Width() != tw || maskImage.Height() != th )
         throw Error( "Mask dimensions after cropping to the preview do not match the target image." );
   }

   std::vector<uint8_t> hole( size_t( tw ) * th );
   repatch::ThresholdMask( maskImage.PixelData( 0 ), tw, th, p_maskThreshold, hole.data() );
   return hole;
}

// ----------------------------------------------------------------------------

bool RepatchInstance::ExecuteOn( View& view )
{
   // Checked once, before branching on the mask source, so Brush mode and
   // Mask image mode reject Neural identically (a script setting mode =
   // Neural together with maskSource = Brush must not silently fall through
   // to the Patch algorithm).
   if ( p_mode == RPMode::Neural )
      throw Error( "Neural mode is not available in this version of Repatch." );

   if ( p_maskSource == RPMaskSource::Brush )
   {
      if ( p_strokes.IsEmpty() )
         throw Error( "No brush strokes to apply." );

      if ( isInterfaceInstance )
      {
         // The interface already applied the strokes; put its result back.
         TheRepatchInterface->RestoreView();
         return true;
      }

      AutoViewLock lock( view );
      ImageVariant image = view.Image();
      if ( image.IsComplexSample() )
         throw Error( "Repatch cannot be executed on complex images." );

      Console console;
      console.EnableAbort();

      uint32 seed = p_randomSeed;
      if ( seed == 0 )
      {
         std::random_device rd;
         do seed = rd(); while ( seed == 0 );
      }

      int strokeCount = 0;
      for ( size_type i = 0; i < p_strokes.Length(); )
      {
         size_type j = i;
         while ( j < p_strokes.Length() && p_strokes[j].strokeIndex == p_strokes[i].strokeIndex )
            ++j;
         repatch::Stroke s = StrokeFromRows( &p_strokes[i], j - i );
         FillStroke( image, s, repatch::DeriveStrokeSeed( seed, strokeCount ) );
         ++strokeCount;
         Module->ProcessEvents();
         if ( console.AbortRequested() )
            throw ProcessAborted();
         i = j;
      }
      console.WriteLn( String().Format( "<end><cbr>Repatch: %d brush stroke(s) applied, seed %u", strokeCount, unsigned( seed ) ) );
      return true;
   }

   AutoViewLock lock( view );

   ImageVariant image = view.Image();
   if ( image.IsComplexSample() )
      throw Error( "Repatch cannot be executed on complex images." );

   Console console;
   console.EnableAbort();

   std::vector<uint8_t> hole = BuildHoleMask( view );

   // Work on float32 planes. 32-bit float images are processed in place.
   const bool needsConversion = !( image.IsFloatSample() && image.BitsPerSample() == 32 );
   Image converted;
   Image* work = nullptr;
   if ( needsConversion )
   {
      ConvertToFloat( image, converted );
      work = &converted;
   }
   else
      work = &static_cast<Image&>( *image );

   const int channels = work->NumberOfNominalChannels();
   if ( channels != 1 && channels != 3 )
      throw Error( String().Format( "Unsupported number of nominal channels: %d", channels ) );

   repatch::FillRequest request;
   request.image.width = work->Width();
   request.image.height = work->Height();
   request.image.channels = channels;
   for ( int c = 0; c < channels; ++c )
      request.image.plane[c] = work->PixelData( c );
   request.mask = hole.data();
   request.params.patchSize = p_patchSize;
   request.params.pyramidLevels = p_pyramidLevelsAuto ? 0 : p_pyramidLevels;
   request.params.iterations = p_iterations;
   request.params.searchRadius = p_searchRadius;
   request.params.sampleRing = p_sampleRing;
   request.params.matchSpace = ( p_matchSpace == RPMatchSpace::Linear ) ? repatch::MatchSpace::Linear
                                                                       : repatch::MatchSpace::Stretched;
   request.params.randomSeed = p_randomSeed;
   request.params.feather = p_feather;
   request.params.threads = Thread::NumberOfThreads( 1024, 1 );

   size_type holePixels = 0;
   for ( uint8_t h : hole )
      holePixels += h;
   console.WriteLn( String().Format( "<end><cbr>Repatch: filling %u pixel(s) with patch size %d (%s matching, %d thread(s))",
                                     unsigned( holePixels ), int( p_patchSize ),
                                     ( p_matchSpace == RPMatchSpace::Linear ) ? "linear" : "stretched",
                                     request.params.threads ) );

   StandardStatus status;
   StatusMonitor monitor;
   monitor.SetCallback( &status );
   monitor.Initialize( "Content-aware fill", 1000 );
   size_type lastCount = 0;
   auto progress = [&]( float fraction, const char* ) -> bool
   {
      try
      {
         size_type count = size_type( Range( double( fraction ), 0.0, 1.0 ) * 1000 );
         if ( count > lastCount )
         {
            monitor += count - lastCount;
            lastCount = count;
         }
      }
      catch ( ProcessAborted& )
      {
         return false;
      }
      Module->ProcessEvents();
      return !console.AbortRequested();
   };

   repatch::PatchBackend backend;
   repatch::FillResult result = backend.Fill( request, progress );
   if ( result.aborted )
      throw ProcessAborted();
   if ( !result.ok )
      throw Error( "Repatch: " + String( result.error.c_str() ) );
   monitor.Complete();

   if ( needsConversion )
      ConvertFromFloat( converted, image );

   console.WriteLn( String().Format( "<end><cbr>Repatch: %d pyramid level(s), seed %u, %.2f s",
                                     result.levelsUsed, unsigned( result.seedUsed ), result.seconds ) );
   return true;
}

// ----------------------------------------------------------------------------

void* RepatchInstance::LockParameter( const MetaParameter* p, size_type tableRow )
{
   if ( p == TheRPModeParameter )
      return &p_mode;
   if ( p == TheRPMaskIdParameter )
      return p_maskId.Begin();
   if ( p == TheRPMaskThresholdParameter )
      return &p_maskThreshold;
   if ( p == TheRPPatchSizeParameter )
      return &p_patchSize;
   if ( p == TheRPPyramidLevelsAutoParameter )
      return &p_pyramidLevelsAuto;
   if ( p == TheRPPyramidLevelsParameter )
      return &p_pyramidLevels;
   if ( p == TheRPIterationsParameter )
      return &p_iterations;
   if ( p == TheRPSearchRadiusParameter )
      return &p_searchRadius;
   if ( p == TheRPSampleRingParameter )
      return &p_sampleRing;
   if ( p == TheRPMatchSpaceParameter )
      return &p_matchSpace;
   if ( p == TheRPRandomSeedParameter )
      return &p_randomSeed;
   if ( p == TheRPFeatherParameter )
      return &p_feather;
   if ( p == TheRPMaskSourceParameter )
      return &p_maskSource;
   if ( p == TheRPBrushRadiusParameter )
      return &p_brushRadius;
   if ( p == TheRPBrushSoftnessParameter )
      return &p_brushSoftness;
   if ( p == TheRPBrushOpacityParameter )
      return &p_brushOpacity;
   if ( p == TheRPStrokeIndexParameter )
      return &p_strokes[tableRow].strokeIndex;
   if ( p == TheRPStrokeXParameter )
      return &p_strokes[tableRow].x;
   if ( p == TheRPStrokeYParameter )
      return &p_strokes[tableRow].y;
   if ( p == TheRPStrokeRadiusParameter )
      return &p_strokes[tableRow].radius;
   if ( p == TheRPStrokeSoftnessParameter )
      return &p_strokes[tableRow].softness;
   if ( p == TheRPStrokeOpacityParameter )
      return &p_strokes[tableRow].opacity;
   return nullptr;
}

bool RepatchInstance::AllocateParameter( size_type sizeOrLength, const MetaParameter* p, size_type /*tableRow*/ )
{
   if ( p == TheRPMaskIdParameter )
   {
      p_maskId.Clear();
      if ( sizeOrLength > 0 )
         p_maskId.SetLength( sizeOrLength );
      return true;
   }
   if ( p == TheRPStrokesParameter )
   {
      p_strokes.Clear();
      if ( sizeOrLength > 0 )
         p_strokes.Add( StrokeRow(), sizeOrLength );
      return true;
   }
   return false;
}

size_type RepatchInstance::ParameterLength( const MetaParameter* p, size_type /*tableRow*/ ) const
{
   if ( p == TheRPMaskIdParameter )
      return p_maskId.Length();
   if ( p == TheRPStrokesParameter )
      return p_strokes.Length();
   return 0;
}

bool RepatchInstance::ValidateParameter( void* value, const MetaParameter* p, size_type /*tableRow*/ ) const
{
   if ( p == TheRPPatchSizeParameter )
   {
      // Patch sizes must be odd; round even values up and keep them in range.
      int32& v = *reinterpret_cast<int32*>( value );
      if ( (v & 1) == 0 )
         ++v;
      v = Range( v, int32( TheRPPatchSizeParameter->MinimumValue() ), int32( TheRPPatchSizeParameter->MaximumValue() ) );
      if ( (v & 1) == 0 )
         --v;
   }
   return true;
}

// ----------------------------------------------------------------------------

} // pcl
