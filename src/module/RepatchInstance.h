#ifndef __RepatchInstance_h
#define __RepatchInstance_h

#include <pcl/Array.h>
#include <pcl/ImageVariant.h>
#include <pcl/MetaParameter.h> // pcl_bool, pcl_enum
#include <pcl/ProcessImplementation.h>
#include <pcl/Rectangle.h>

#include <vector>

#include "repatch/FillBackend.h"
#include "repatch/Stroke.h"

namespace pcl
{

class RepatchInstance : public ProcessImplementation
{
public:

   /*
    * One row of the strokes table: a point of a brush stroke.
    */
   struct StrokeRow
   {
      int32 strokeIndex = 0;
      float x = 0;
      float y = 0;
      int32 radius = 15;
      float softness = 0.5F;
      float opacity = 1.0F;
   };

   RepatchInstance( const MetaProcess* );
   RepatchInstance( const RepatchInstance& );

   void Assign( const ProcessImplementation& ) override;
   UndoFlags UndoMode( const View& ) const override;
   bool CanExecuteOn( const View&, pcl::String& whyNot ) const override;
   bool ExecuteOn( View& ) override;
   void* LockParameter( const MetaParameter*, size_type tableRow ) override;
   bool AllocateParameter( size_type sizeOrLength, const MetaParameter* p, size_type tableRow ) override;
   size_type ParameterLength( const MetaParameter* p, size_type tableRow ) const override;
   bool ValidateParameter( void* value, const MetaParameter* p, size_type tableRow ) const override;

   /*
    * Core parameters derived from this instance (seed and sampleRing are
    * left as stored; callers override them per stroke).
    */
   repatch::FillParams CoreParams() const;

   /*
    * Fills one brush stroke in place on the given image with the given seed.
    * Returns the rectangle that was modified. Throws Error on failure. Used
    * by the interface for the live preview and by ExecuteOn for replay, so
    * both produce identical pixels.
    */
   Rect FillStroke( ImageVariant& image, const repatch::Stroke& stroke, uint32 strokeSeed ) const;

   static repatch::Stroke StrokeFromRows( const StrokeRow* rows, size_type count );

   /*
    * Same-type copy of a rectangle of an image / paste of such a tile.
    */
   static ImageVariant CopyTile( const ImageVariant& image, const Rect& r );
   static void PasteTile( ImageVariant& image, const Rect& at, const ImageVariant& tile );

private:

   pcl_enum p_mode;
   String   p_maskId;
   float    p_maskThreshold;
   int32    p_patchSize;
   pcl_bool p_pyramidLevelsAuto;
   int32    p_pyramidLevels;
   int32    p_iterations;
   int32    p_searchRadius;
   int32    p_sampleRing;
   pcl_enum p_matchSpace;
   uint32   p_randomSeed;
   int32    p_feather;
   pcl_enum p_maskSource;
   int32    p_brushRadius;
   float    p_brushSoftness;
   float    p_brushOpacity;
   Array<StrokeRow> p_strokes;

   /*
    * Set by RepatchInterface::Execute(): the strokes have already been
    * applied by the interface; ExecuteOn just restores the processed pixels
    * (CloneStamp's commit pattern). Never copied by Assign().
    */
   bool isInterfaceInstance = false;

   /*
    * Builds the binary hole mask (1 = fill) for the target view from the mask
    * image identified by p_maskId, cropping it to the preview rectangle when
    * the target is a preview. Mask-image mode only.
    */
   std::vector<uint8_t> BuildHoleMask( const View& target ) const;

   friend class RepatchProcess;
   friend class RepatchInterface;
};

} // pcl

#endif   // __RepatchInstance_h
