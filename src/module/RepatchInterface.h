#ifndef __RepatchInterface_h
#define __RepatchInterface_h

#include <pcl/Array.h>
#include <pcl/CheckBox.h>
#include <pcl/ComboBox.h>
#include <pcl/Control.h>
#include <pcl/ImageVariant.h>
#include <pcl/Label.h>
#include <pcl/NumericControl.h>
#include <pcl/ProcessInterface.h>
#include <pcl/PushButton.h>
#include <pcl/RadioButton.h>
#include <pcl/Rectangle.h>
#include <pcl/SectionBar.h>
#include <pcl/Sizer.h>
#include <pcl/SpinBox.h>
#include <pcl/ViewList.h>

#include <vector>

#include "RepatchInstance.h"

namespace pcl
{

class RepatchInterface : public ProcessInterface
{
public:

   RepatchInterface();
   virtual ~RepatchInterface();

   IsoString Id() const override;
   MetaProcess* Process() const override;
   IsoString IconImageSVG() const override;
   InterfaceFeatures Features() const override;
   void ApplyInstance() const override;
   void Execute() override;
   void Cancel() override;
   void ResetInstance() override;
   bool Launch( const MetaProcess&, const ProcessImplementation*, bool& dynamic, unsigned& /*flags*/ ) override;
   ProcessImplementation* NewProcess() const override;
   bool ValidateProcess( const ProcessImplementation&, String& whyNot ) const override;
   bool RequiresInstanceValidation() const override;
   bool ImportProcess( const ProcessImplementation& ) override;
   bool WantsImageNotifications() const override;
   void ImageCreated( const View& ) override;
   void ImageDeleted( const View& ) override;
   void ImageRenamed( const View& ) override;

   bool IsDynamicInterface() const override;
   void ExitDynamicMode() override;
   void DynamicMouseEnter( View& ) override;
   void DynamicMouseLeave( View& ) override;
   void DynamicMouseMove( View&, const DPoint&, unsigned buttons, unsigned modifiers ) override;
   void DynamicMousePress( View&, const DPoint&, int button, unsigned buttons, unsigned modifiers ) override;
   void DynamicMouseRelease( View&, const DPoint&, int button, unsigned buttons, unsigned modifiers ) override;
   bool DynamicKeyPress( View&, int key, unsigned modifiers ) override;
   bool RequiresDynamicUpdate( const View&, const DRect& ) const override;
   void DynamicPaint( const View&, VectorGraphics&, const DRect& ) const override;

   /*
    * Called by RepatchInstance::ExecuteOn() when the instance was launched by
    * Execute(): puts the already-computed stroke results back into the view.
    */
   void RestoreView();

private:

   RepatchInstance m_instance;

   /*
    * One stroke of the current painting session.
    */
   struct SessionStroke
   {
      repatch::Stroke stroke;
      Rect            roi;     // rectangle modified by the fill
      ImageVariant    before;  // roi pixels before the stroke
      ImageVariant    after;   // roi pixels after the stroke
   };

   View*                      m_targetView = nullptr;
   RepatchInstance            m_session;        // parameters + seed frozen at the first stroke
   std::vector<SessionStroke> m_done;
   std::vector<SessionStroke> m_undone;
   Array<DPoint>              m_path;           // stroke being dragged
   DPoint                     m_cursor = 0;
   bool                       m_onTarget = false;
   bool                       m_dragging = false;
   bool                       m_executing = false;

   struct GUIData
   {
      GUIData( RepatchInterface& );

      VerticalSizer     Global_Sizer;

         SectionBar        Mode_SectionBar;
         Control           Mode_Control;
         HorizontalSizer   Mode_Sizer;
            RadioButton       Patch_RadioButton;
            RadioButton       Neural_RadioButton;

         SectionBar        MaskSource_SectionBar;
         Control           MaskSource_Control;
         HorizontalSizer   MaskSource_Sizer;
            RadioButton       Brush_RadioButton;
            RadioButton       MaskImage_RadioButton;

         SectionBar        Brush_SectionBar;
         Control           Brush_Control;
         HorizontalSizer   Brush_Sizer;
            VerticalSizer     BrushParameters_Sizer;
               HorizontalSizer   BrushRadius_Sizer;
                  Label             BrushRadius_Label;
                  SpinBox           BrushRadius_SpinBox;
               NumericControl    BrushSoftness_NumericControl;
               NumericControl    BrushOpacity_NumericControl;
               HorizontalSizer   BrushActions_Sizer;
                  PushButton        UndoStroke_PushButton;
                  PushButton        RedoStroke_PushButton;
                  PushButton        ClearStrokes_PushButton;
               Label             Target_Label;
            Control           BrushPreview_Control;

         SectionBar        Mask_SectionBar;
         Control           Mask_Control;
         VerticalSizer     Mask_Sizer;
            HorizontalSizer   MaskView_Sizer;
               Label             MaskView_Label;
               ViewList          Mask_ViewList;
            NumericControl    MaskThreshold_NumericControl;

         SectionBar        Patch_SectionBar;
         Control           Patch_Control;
         VerticalSizer     Patch_Sizer;
            HorizontalSizer   PatchSize_Sizer;
               Label             PatchSize_Label;
               SpinBox           PatchSize_SpinBox;
            HorizontalSizer   PyramidLevels_Sizer;
               Label             PyramidLevels_Label;
               SpinBox           PyramidLevels_SpinBox;
               CheckBox          PyramidLevelsAuto_CheckBox;
            HorizontalSizer   Iterations_Sizer;
               Label             Iterations_Label;
               SpinBox           Iterations_SpinBox;

         SectionBar        Sampling_SectionBar;
         Control           Sampling_Control;
         VerticalSizer     Sampling_Sizer;
            HorizontalSizer   SearchRadius_Sizer;
               Label             SearchRadius_Label;
               SpinBox           SearchRadius_SpinBox;
            HorizontalSizer   SampleRing_Sizer;
               Label             SampleRing_Label;
               SpinBox           SampleRing_SpinBox;

         SectionBar        Output_SectionBar;
         Control           Output_Control;
         VerticalSizer     Output_Sizer;
            HorizontalSizer   MatchSpace_Sizer;
               Label             MatchSpace_Label;
               ComboBox          MatchSpace_ComboBox;
            HorizontalSizer   Feather_Sizer;
               Label             Feather_Label;
               SpinBox           Feather_SpinBox;
            HorizontalSizer   RandomSeed_Sizer;
               NumericEdit       RandomSeed_NumericEdit;
   };

   GUIData* GUI = nullptr;

   void UpdateControls();
   void UpdateModeControls();
   void UpdateTargetInfo();

   bool BrushMode() const;
   bool SessionActive() const { return m_targetView != nullptr; }
   void SelectTarget( View& );
   void ResetSession();
   void RestoreOriginal();
   Rect PathBounds( const Array<DPoint>&, int radius ) const;
   void InvalidateCursor();
   void InvalidatePath();
   void AppendPathPoint( const DPoint& );
   bool CommitStroke();
   void UndoStroke();
   void RedoStroke();
   void ClearStrokes();
   void ExportStrokes( RepatchInstance& ) const;
   void SetBrushRadius( int );

   void e_ModeClicked( Button& sender, bool checked );
   void e_MaskSourceClicked( Button& sender, bool checked );
   void e_ViewSelected( ViewList& sender, View& view );
   void e_RealValueUpdated( NumericEdit& sender, double value );
   void e_IntegerValueUpdated( SpinBox& sender, int value );
   void e_CheckClicked( Button& sender, bool checked );
   void e_ItemSelected( ComboBox& sender, int itemIndex );
   void e_StrokeButtonClicked( Button& sender, bool checked );
   void e_BrushPreviewPaint( Control& sender, const Rect& updateRect );

   friend struct GUIData;
};

PCL_BEGIN_LOCAL
extern RepatchInterface* TheRepatchInterface;
PCL_END_LOCAL

} // pcl

#endif   // __RepatchInterface_h
