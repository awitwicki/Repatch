#ifndef __RepatchParameters_h
#define __RepatchParameters_h

#include <pcl/MetaParameter.h>

namespace pcl
{

PCL_BEGIN_LOCAL

// ----------------------------------------------------------------------------

class RPMode : public MetaEnumeration
{
public:

   enum { Patch,
          Neural,
          NumberOfItems,
          Default = Patch };

   RPMode( MetaProcess* );

   IsoString Id() const override;
   size_type NumberOfElements() const override;
   IsoString ElementId( size_type ) const override;
   int ElementValue( size_type ) const override;
   size_type DefaultValueIndex() const override;
};

extern RPMode* TheRPModeParameter;

// ----------------------------------------------------------------------------

class RPMaskId : public MetaString
{
public:

   RPMaskId( MetaProcess* );

   IsoString Id() const override;
};

extern RPMaskId* TheRPMaskIdParameter;

// ----------------------------------------------------------------------------

class RPMaskThreshold : public MetaFloat
{
public:

   RPMaskThreshold( MetaProcess* );

   IsoString Id() const override;
   int Precision() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPMaskThreshold* TheRPMaskThresholdParameter;

// ----------------------------------------------------------------------------

class RPPatchSize : public MetaInt32
{
public:

   RPPatchSize( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPPatchSize* TheRPPatchSizeParameter;

// ----------------------------------------------------------------------------

class RPPyramidLevelsAuto : public MetaBoolean
{
public:

   RPPyramidLevelsAuto( MetaProcess* );

   IsoString Id() const override;
   bool DefaultValue() const override;
};

extern RPPyramidLevelsAuto* TheRPPyramidLevelsAutoParameter;

// ----------------------------------------------------------------------------

class RPPyramidLevels : public MetaInt32
{
public:

   RPPyramidLevels( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPPyramidLevels* TheRPPyramidLevelsParameter;

// ----------------------------------------------------------------------------

class RPIterations : public MetaInt32
{
public:

   RPIterations( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPIterations* TheRPIterationsParameter;

// ----------------------------------------------------------------------------

class RPSearchRadius : public MetaInt32
{
public:

   RPSearchRadius( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPSearchRadius* TheRPSearchRadiusParameter;

// ----------------------------------------------------------------------------

class RPSampleRing : public MetaInt32
{
public:

   RPSampleRing( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPSampleRing* TheRPSampleRingParameter;

// ----------------------------------------------------------------------------

class RPMatchSpace : public MetaEnumeration
{
public:

   enum { Linear,
          Stretched,
          NumberOfItems,
          Default = Stretched };

   RPMatchSpace( MetaProcess* );

   IsoString Id() const override;
   size_type NumberOfElements() const override;
   IsoString ElementId( size_type ) const override;
   int ElementValue( size_type ) const override;
   size_type DefaultValueIndex() const override;
};

extern RPMatchSpace* TheRPMatchSpaceParameter;

// ----------------------------------------------------------------------------

class RPRandomSeed : public MetaUInt32
{
public:

   RPRandomSeed( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPRandomSeed* TheRPRandomSeedParameter;

// ----------------------------------------------------------------------------

class RPFeather : public MetaInt32
{
public:

   RPFeather( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPFeather* TheRPFeatherParameter;

// ----------------------------------------------------------------------------

class RPMaskSource : public MetaEnumeration
{
public:

   enum { Brush,
          MaskImage,
          NumberOfItems,
          Default = Brush };

   RPMaskSource( MetaProcess* );

   IsoString Id() const override;
   size_type NumberOfElements() const override;
   IsoString ElementId( size_type ) const override;
   int ElementValue( size_type ) const override;
   size_type DefaultValueIndex() const override;
};

extern RPMaskSource* TheRPMaskSourceParameter;

// ----------------------------------------------------------------------------

class RPBrushRadius : public MetaInt32
{
public:

   RPBrushRadius( MetaProcess* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPBrushRadius* TheRPBrushRadiusParameter;

// ----------------------------------------------------------------------------

class RPBrushSoftness : public MetaFloat
{
public:

   RPBrushSoftness( MetaProcess* );

   IsoString Id() const override;
   int Precision() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPBrushSoftness* TheRPBrushSoftnessParameter;

// ----------------------------------------------------------------------------

class RPBrushOpacity : public MetaFloat
{
public:

   RPBrushOpacity( MetaProcess* );

   IsoString Id() const override;
   int Precision() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPBrushOpacity* TheRPBrushOpacityParameter;

// ----------------------------------------------------------------------------

class RPStrokes : public MetaTable
{
public:

   RPStrokes( MetaProcess* );

   IsoString Id() const override;
};

extern RPStrokes* TheRPStrokesParameter;

class RPStrokeIndex : public MetaInt32
{
public:

   RPStrokeIndex( MetaTable* );

   IsoString Id() const override;
   double MinimumValue() const override;
};

extern RPStrokeIndex* TheRPStrokeIndexParameter;

class RPStrokeX : public MetaFloat
{
public:

   RPStrokeX( MetaTable* );

   IsoString Id() const override;
   int Precision() const override;
};

extern RPStrokeX* TheRPStrokeXParameter;

class RPStrokeY : public MetaFloat
{
public:

   RPStrokeY( MetaTable* );

   IsoString Id() const override;
   int Precision() const override;
};

extern RPStrokeY* TheRPStrokeYParameter;

class RPStrokeRadius : public MetaInt32
{
public:

   RPStrokeRadius( MetaTable* );

   IsoString Id() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPStrokeRadius* TheRPStrokeRadiusParameter;

class RPStrokeSoftness : public MetaFloat
{
public:

   RPStrokeSoftness( MetaTable* );

   IsoString Id() const override;
   int Precision() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPStrokeSoftness* TheRPStrokeSoftnessParameter;

class RPStrokeOpacity : public MetaFloat
{
public:

   RPStrokeOpacity( MetaTable* );

   IsoString Id() const override;
   int Precision() const override;
   double DefaultValue() const override;
   double MinimumValue() const override;
   double MaximumValue() const override;
};

extern RPStrokeOpacity* TheRPStrokeOpacityParameter;

// ----------------------------------------------------------------------------

PCL_END_LOCAL

} // pcl

#endif   // __RepatchParameters_h
