#include "RepatchProcess.h"
#include "RepatchIcon.h"
#include "RepatchInterface.h"
#include "RepatchInstance.h"
#include "RepatchParameters.h"

namespace pcl
{

RepatchProcess* TheRepatchProcess = nullptr;

// ----------------------------------------------------------------------------

RepatchProcess::RepatchProcess()
{
   TheRepatchProcess = this;

   new RPMode( this );
   new RPMaskId( this );
   new RPMaskThreshold( this );
   new RPPatchSize( this );
   new RPPyramidLevelsAuto( this );
   new RPPyramidLevels( this );
   new RPIterations( this );
   new RPSearchRadius( this );
   new RPSampleRing( this );
   new RPMatchSpace( this );
   new RPRandomSeed( this );
   new RPFeather( this );
   new RPMaskSource( this );
   new RPBrushRadius( this );
   new RPBrushSoftness( this );
   new RPBrushOpacity( this );
   new RPStrokes( this );
   new RPStrokeIndex( TheRPStrokesParameter );
   new RPStrokeX( TheRPStrokesParameter );
   new RPStrokeY( TheRPStrokesParameter );
   new RPStrokeRadius( TheRPStrokesParameter );
   new RPStrokeSoftness( TheRPStrokesParameter );
   new RPStrokeOpacity( TheRPStrokesParameter );
}

IsoString RepatchProcess::Id() const
{
   return "Repatch";
}

IsoString RepatchProcess::Categories() const
{
   return "Painting";
}

uint32 RepatchProcess::Version() const
{
   return 0x100;
}

String RepatchProcess::Description() const
{
   return "Content-aware fill using the PatchMatch algorithm (Barnes et al. 2009). Fills a masked region "
          "with plausible content synthesized from the surrounding image.";
}

IsoString RepatchProcess::IconImageSVG() const
{
   return kRepatchIconSVG;
}

ProcessInterface* RepatchProcess::DefaultInterface() const
{
   return TheRepatchInterface;
}

ProcessImplementation* RepatchProcess::Create() const
{
   return new RepatchInstance( this );
}

ProcessImplementation* RepatchProcess::Clone( const ProcessImplementation& p ) const
{
   const RepatchInstance* instance = dynamic_cast<const RepatchInstance*>( &p );
   return (instance != nullptr) ? new RepatchInstance( *instance ) : nullptr;
}

// ----------------------------------------------------------------------------

} // pcl
