#include "RepatchParameters.h"

namespace pcl
{

RPMode*              TheRPModeParameter = nullptr;
RPMaskId*            TheRPMaskIdParameter = nullptr;
RPMaskThreshold*     TheRPMaskThresholdParameter = nullptr;
RPPatchSize*         TheRPPatchSizeParameter = nullptr;
RPPyramidLevelsAuto* TheRPPyramidLevelsAutoParameter = nullptr;
RPPyramidLevels*     TheRPPyramidLevelsParameter = nullptr;
RPIterations*        TheRPIterationsParameter = nullptr;
RPSearchRadius*      TheRPSearchRadiusParameter = nullptr;
RPSampleRing*        TheRPSampleRingParameter = nullptr;
RPMatchSpace*        TheRPMatchSpaceParameter = nullptr;
RPRandomSeed*        TheRPRandomSeedParameter = nullptr;
RPFeather*           TheRPFeatherParameter = nullptr;
RPMaskSource*        TheRPMaskSourceParameter = nullptr;
RPBrushRadius*       TheRPBrushRadiusParameter = nullptr;
RPBrushSoftness*     TheRPBrushSoftnessParameter = nullptr;
RPBrushOpacity*      TheRPBrushOpacityParameter = nullptr;
RPStrokes*           TheRPStrokesParameter = nullptr;
RPStrokeIndex*       TheRPStrokeIndexParameter = nullptr;
RPStrokeX*           TheRPStrokeXParameter = nullptr;
RPStrokeY*           TheRPStrokeYParameter = nullptr;
RPStrokeRadius*      TheRPStrokeRadiusParameter = nullptr;
RPStrokeSoftness*    TheRPStrokeSoftnessParameter = nullptr;
RPStrokeOpacity*     TheRPStrokeOpacityParameter = nullptr;

// ----------------------------------------------------------------------------

RPMode::RPMode( MetaProcess* P ) : MetaEnumeration( P )
{
   TheRPModeParameter = this;
}

IsoString RPMode::Id() const
{
   return "mode";
}

size_type RPMode::NumberOfElements() const
{
   return NumberOfItems;
}

IsoString RPMode::ElementId( size_type i ) const
{
   switch ( i )
   {
   default:
   case Patch:  return "Patch";
   case Neural: return "Neural";
   }
}

int RPMode::ElementValue( size_type i ) const
{
   return int( i );
}

size_type RPMode::DefaultValueIndex() const
{
   return Default;
}

// ----------------------------------------------------------------------------

RPMaskId::RPMaskId( MetaProcess* P ) : MetaString( P )
{
   TheRPMaskIdParameter = this;
}

IsoString RPMaskId::Id() const
{
   return "maskId";
}

// ----------------------------------------------------------------------------

RPMaskThreshold::RPMaskThreshold( MetaProcess* P ) : MetaFloat( P )
{
   TheRPMaskThresholdParameter = this;
}

IsoString RPMaskThreshold::Id() const
{
   return "maskThreshold";
}

int RPMaskThreshold::Precision() const
{
   return 3;
}

double RPMaskThreshold::DefaultValue() const
{
   return 0.5;
}

double RPMaskThreshold::MinimumValue() const
{
   return 0;
}

double RPMaskThreshold::MaximumValue() const
{
   return 1;
}

// ----------------------------------------------------------------------------

RPPatchSize::RPPatchSize( MetaProcess* P ) : MetaInt32( P )
{
   TheRPPatchSizeParameter = this;
}

IsoString RPPatchSize::Id() const
{
   return "patchSize";
}

double RPPatchSize::DefaultValue() const
{
   return 11;
}

double RPPatchSize::MinimumValue() const
{
   return 5;
}

double RPPatchSize::MaximumValue() const
{
   return 41;
}

// ----------------------------------------------------------------------------

RPPyramidLevelsAuto::RPPyramidLevelsAuto( MetaProcess* P ) : MetaBoolean( P )
{
   TheRPPyramidLevelsAutoParameter = this;
}

IsoString RPPyramidLevelsAuto::Id() const
{
   return "pyramidLevelsAuto";
}

bool RPPyramidLevelsAuto::DefaultValue() const
{
   return true;
}

// ----------------------------------------------------------------------------

RPPyramidLevels::RPPyramidLevels( MetaProcess* P ) : MetaInt32( P )
{
   TheRPPyramidLevelsParameter = this;
}

IsoString RPPyramidLevels::Id() const
{
   return "pyramidLevels";
}

double RPPyramidLevels::DefaultValue() const
{
   return 4;
}

double RPPyramidLevels::MinimumValue() const
{
   return 1;
}

double RPPyramidLevels::MaximumValue() const
{
   return 8;
}

// ----------------------------------------------------------------------------

RPIterations::RPIterations( MetaProcess* P ) : MetaInt32( P )
{
   TheRPIterationsParameter = this;
}

IsoString RPIterations::Id() const
{
   return "iterations";
}

double RPIterations::DefaultValue() const
{
   return 5;
}

double RPIterations::MinimumValue() const
{
   return 1;
}

double RPIterations::MaximumValue() const
{
   return 50;
}

// ----------------------------------------------------------------------------

RPSearchRadius::RPSearchRadius( MetaProcess* P ) : MetaInt32( P )
{
   TheRPSearchRadiusParameter = this;
}

IsoString RPSearchRadius::Id() const
{
   return "searchRadius";
}

double RPSearchRadius::DefaultValue() const
{
   return 0;
}

double RPSearchRadius::MinimumValue() const
{
   return 0;
}

double RPSearchRadius::MaximumValue() const
{
   return 65535;
}

// ----------------------------------------------------------------------------

RPSampleRing::RPSampleRing( MetaProcess* P ) : MetaInt32( P )
{
   TheRPSampleRingParameter = this;
}

IsoString RPSampleRing::Id() const
{
   return "sampleRing";
}

double RPSampleRing::DefaultValue() const
{
   return 0;
}

double RPSampleRing::MinimumValue() const
{
   return 0;
}

double RPSampleRing::MaximumValue() const
{
   return 65535;
}

// ----------------------------------------------------------------------------

RPMatchSpace::RPMatchSpace( MetaProcess* P ) : MetaEnumeration( P )
{
   TheRPMatchSpaceParameter = this;
}

IsoString RPMatchSpace::Id() const
{
   return "matchSpace";
}

size_type RPMatchSpace::NumberOfElements() const
{
   return NumberOfItems;
}

IsoString RPMatchSpace::ElementId( size_type i ) const
{
   switch ( i )
   {
   case Linear:    return "Linear";
   default:
   case Stretched: return "Stretched";
   }
}

int RPMatchSpace::ElementValue( size_type i ) const
{
   return int( i );
}

size_type RPMatchSpace::DefaultValueIndex() const
{
   return Default;
}

// ----------------------------------------------------------------------------

RPRandomSeed::RPRandomSeed( MetaProcess* P ) : MetaUInt32( P )
{
   TheRPRandomSeedParameter = this;
}

IsoString RPRandomSeed::Id() const
{
   return "randomSeed";
}

double RPRandomSeed::DefaultValue() const
{
   return 0;
}

double RPRandomSeed::MinimumValue() const
{
   return 0;
}

double RPRandomSeed::MaximumValue() const
{
   return 4294967295.0;
}

// ----------------------------------------------------------------------------

RPFeather::RPFeather( MetaProcess* P ) : MetaInt32( P )
{
   TheRPFeatherParameter = this;
}

IsoString RPFeather::Id() const
{
   return "feather";
}

double RPFeather::DefaultValue() const
{
   return 3;
}

double RPFeather::MinimumValue() const
{
   return 0;
}

double RPFeather::MaximumValue() const
{
   return 256;
}

// ----------------------------------------------------------------------------

RPMaskSource::RPMaskSource( MetaProcess* P ) : MetaEnumeration( P )
{
   TheRPMaskSourceParameter = this;
}

IsoString RPMaskSource::Id() const
{
   return "maskSource";
}

size_type RPMaskSource::NumberOfElements() const
{
   return NumberOfItems;
}

IsoString RPMaskSource::ElementId( size_type i ) const
{
   switch ( i )
   {
   default:
   case Brush:     return "Brush";
   case MaskImage: return "MaskImage";
   }
}

int RPMaskSource::ElementValue( size_type i ) const
{
   return int( i );
}

size_type RPMaskSource::DefaultValueIndex() const
{
   return Default;
}

// ----------------------------------------------------------------------------

RPBrushRadius::RPBrushRadius( MetaProcess* P ) : MetaInt32( P )
{
   TheRPBrushRadiusParameter = this;
}

IsoString RPBrushRadius::Id() const
{
   return "brushRadius";
}

double RPBrushRadius::DefaultValue() const
{
   return 15;
}

double RPBrushRadius::MinimumValue() const
{
   return 1;
}

double RPBrushRadius::MaximumValue() const
{
   return 500;
}

// ----------------------------------------------------------------------------

RPBrushSoftness::RPBrushSoftness( MetaProcess* P ) : MetaFloat( P )
{
   TheRPBrushSoftnessParameter = this;
}

IsoString RPBrushSoftness::Id() const
{
   return "brushSoftness";
}

int RPBrushSoftness::Precision() const
{
   return 2;
}

double RPBrushSoftness::DefaultValue() const
{
   return 0.5;
}

double RPBrushSoftness::MinimumValue() const
{
   return 0;
}

double RPBrushSoftness::MaximumValue() const
{
   return 1;
}

// ----------------------------------------------------------------------------

RPBrushOpacity::RPBrushOpacity( MetaProcess* P ) : MetaFloat( P )
{
   TheRPBrushOpacityParameter = this;
}

IsoString RPBrushOpacity::Id() const
{
   return "brushOpacity";
}

int RPBrushOpacity::Precision() const
{
   return 2;
}

double RPBrushOpacity::DefaultValue() const
{
   return 1;
}

double RPBrushOpacity::MinimumValue() const
{
   return 0;
}

double RPBrushOpacity::MaximumValue() const
{
   return 1;
}

// ----------------------------------------------------------------------------

RPStrokes::RPStrokes( MetaProcess* P ) : MetaTable( P )
{
   TheRPStrokesParameter = this;
}

IsoString RPStrokes::Id() const
{
   return "strokes";
}

// ----------------------------------------------------------------------------

RPStrokeIndex::RPStrokeIndex( MetaTable* T ) : MetaInt32( T )
{
   TheRPStrokeIndexParameter = this;
}

IsoString RPStrokeIndex::Id() const
{
   return "strokeIndex";
}

double RPStrokeIndex::MinimumValue() const
{
   return 0;
}

// ----------------------------------------------------------------------------

RPStrokeX::RPStrokeX( MetaTable* T ) : MetaFloat( T )
{
   TheRPStrokeXParameter = this;
}

IsoString RPStrokeX::Id() const
{
   return "x";
}

int RPStrokeX::Precision() const
{
   return 2;
}

// ----------------------------------------------------------------------------

RPStrokeY::RPStrokeY( MetaTable* T ) : MetaFloat( T )
{
   TheRPStrokeYParameter = this;
}

IsoString RPStrokeY::Id() const
{
   return "y";
}

int RPStrokeY::Precision() const
{
   return 2;
}

// ----------------------------------------------------------------------------

RPStrokeRadius::RPStrokeRadius( MetaTable* T ) : MetaInt32( T )
{
   TheRPStrokeRadiusParameter = this;
}

IsoString RPStrokeRadius::Id() const
{
   return "radius";
}

double RPStrokeRadius::DefaultValue() const
{
   return 15;
}

double RPStrokeRadius::MinimumValue() const
{
   return 1;
}

double RPStrokeRadius::MaximumValue() const
{
   return 500;
}

// ----------------------------------------------------------------------------

RPStrokeSoftness::RPStrokeSoftness( MetaTable* T ) : MetaFloat( T )
{
   TheRPStrokeSoftnessParameter = this;
}

IsoString RPStrokeSoftness::Id() const
{
   return "softness";
}

int RPStrokeSoftness::Precision() const
{
   return 2;
}

double RPStrokeSoftness::DefaultValue() const
{
   return 0.5;
}

double RPStrokeSoftness::MinimumValue() const
{
   return 0;
}

double RPStrokeSoftness::MaximumValue() const
{
   return 1;
}

// ----------------------------------------------------------------------------

RPStrokeOpacity::RPStrokeOpacity( MetaTable* T ) : MetaFloat( T )
{
   TheRPStrokeOpacityParameter = this;
}

IsoString RPStrokeOpacity::Id() const
{
   return "opacity";
}

int RPStrokeOpacity::Precision() const
{
   return 2;
}

double RPStrokeOpacity::DefaultValue() const
{
   return 1;
}

double RPStrokeOpacity::MinimumValue() const
{
   return 0;
}

double RPStrokeOpacity::MaximumValue() const
{
   return 1;
}

// ----------------------------------------------------------------------------

} // pcl
