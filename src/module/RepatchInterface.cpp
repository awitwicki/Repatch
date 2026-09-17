#include "RepatchInterface.h"
#include "RepatchIcon.h"
#include "RepatchParameters.h"
#include "RepatchProcess.h"

#include <pcl/Bitmap.h>
#include <pcl/Console.h>
#include <pcl/Graphics.h>
#include <pcl/ImageWindow.h>
#include <pcl/KeyCodes.h>
#include <pcl/MessageBox.h>
#include <pcl/MetaModule.h>
#include <pcl/Pen.h>

#include <cmath>
#include <random>

#include "repatch/Stroke.h"

namespace pcl
{

RepatchInterface* TheRepatchInterface = nullptr;

// ----------------------------------------------------------------------------

RepatchInterface::RepatchInterface()
   : m_instance( TheRepatchProcess )
   , m_session( TheRepatchProcess )
{
   TheRepatchInterface = this;
}

RepatchInterface::~RepatchInterface()
{
   ResetSession();
   if ( GUI != nullptr )
      delete GUI, GUI = nullptr;
}

IsoString RepatchInterface::Id() const
{
   return "Repatch";
}

MetaProcess* RepatchInterface::Process() const
{
   return TheRepatchProcess;
}

IsoString RepatchInterface::IconImageSVG() const
{
   return kRepatchIconSVG;
}

InterfaceFeatures RepatchInterface::Features() const
{
   return InterfaceFeature::DefaultDynamic;
}

void RepatchInterface::ApplyInstance() const
{
   m_instance.LaunchOnCurrentView();
}

bool RepatchInterface::BrushMode() const
{
   return m_instance.p_maskSource == RPMaskSource::Brush;
}

// ----------------------------------------------------------------------------

void RepatchInterface::Execute()
{
   if ( !BrushMode() )
   {
      m_instance.LaunchOnCurrentView();
      return;
   }
   if ( !SessionActive() || m_done.empty() )
   {
      MessageBox( "<p>No brush strokes to apply. Paint on the target image first.</p>",
                  "Repatch", StdIcon::Information, StdButton::Ok ).Execute();
      return;
   }

   m_executing = true;

   // Put the original pixels back; the 'after' tiles are re-applied by
   // RestoreView() from inside the instance's ExecuteOn() (CloneStamp pattern).
   ImageVariant image = m_targetView->Image();
   for ( auto it = m_done.rbegin(); it != m_done.rend(); ++it )
      RepatchInstance::PasteTile( image, it->roi, it->before );

   RepatchInstance instance( m_session );
   ExportStrokes( instance );
   instance.isInterfaceInstance = true;

   View v = *m_targetView;
   ImageWindow w = v.Window();

   m_targetView->RemoveFromDynamicTargets();
   m_onTarget = false;

   w.BringToFront();
   w.SelectView( v );
   instance.LaunchOn( w );

   ResetSession();
   UpdateControls();
}

void RepatchInterface::Cancel()
{
   if ( SessionActive() && !m_done.empty() )
      if ( MessageBox( "<p><b>Existing Repatch strokes will be lost.</b></p>"
                       "<p>This cannot be undone. Cancel the active Repatch session?</p>",
                       "Repatch", StdIcon::Warning, StdButton::No, StdButton::Yes ).Execute() != StdButton::Yes )
         return;
   RestoreOriginal();
   ResetSession();
   UpdateControls();
}

void RepatchInterface::RestoreView()
{
   if ( m_targetView == nullptr )
      return;
   ImageVariant image = m_targetView->Image();
   for ( const SessionStroke& s : m_done )
      RepatchInstance::PasteTile( image, s.roi, s.after );
}

// ----------------------------------------------------------------------------

void RepatchInterface::ResetInstance()
{
   RepatchInstance defaultInstance( TheRepatchProcess );
   ImportProcess( defaultInstance );
}

bool RepatchInterface::Launch( const MetaProcess& P, const ProcessImplementation*, bool& dynamic, unsigned& /*flags*/ )
{
   if ( GUI == nullptr )
   {
      GUI = new GUIData( *this );
      SetWindowTitle( "Repatch" );
   }
   GUI->Mask_ViewList.Regenerate( true/*mainViews*/, false/*previews*/ );
   UpdateControls();

   dynamic = true;
   return &P == TheRepatchProcess;
}

ProcessImplementation* RepatchInterface::NewProcess() const
{
   RepatchInstance* instance = new RepatchInstance( m_instance );
   if ( SessionActive() && !m_done.empty() )
   {
      instance->Assign( m_session );
      ExportStrokes( *instance );
   }
   return instance;
}

bool RepatchInterface::ValidateProcess( const ProcessImplementation& p, String& whyNot ) const
{
   if ( dynamic_cast<const RepatchInstance*>( &p ) != nullptr )
      return true;
   whyNot = "Not a Repatch instance.";
   return false;
}

bool RepatchInterface::RequiresInstanceValidation() const
{
   return true;
}

bool RepatchInterface::ImportProcess( const ProcessImplementation& p )
{
   m_instance.Assign( p );
   m_instance.p_strokes.Clear(); // strokes belong to a painting session, not to the editor
   UpdateControls();
   return true;
}

bool RepatchInterface::WantsImageNotifications() const
{
   return true;
}

void RepatchInterface::ImageCreated( const View& )
{
   if ( GUI != nullptr )
   {
      GUI->Mask_ViewList.Regenerate( true, false );
      UpdateControls();
   }
}

void RepatchInterface::ImageDeleted( const View& v )
{
   if ( SessionActive() && v == *m_targetView )
   {
      ResetSession();
   }
   if ( GUI != nullptr )
   {
      GUI->Mask_ViewList.Regenerate( true, false );
      UpdateControls();
   }
}

void RepatchInterface::ImageRenamed( const View& )
{
   if ( GUI != nullptr )
   {
      GUI->Mask_ViewList.Regenerate( true, false );
      UpdateControls();
   }
}

// ----------------------------------------------------------------------------
// Dynamic interface
// ----------------------------------------------------------------------------

bool RepatchInterface::IsDynamicInterface() const
{
   return true;
}

void RepatchInterface::ExitDynamicMode()
{
   RestoreOriginal();
   ResetSession();
   if ( GUI != nullptr )
      UpdateControls();
}

void RepatchInterface::SelectTarget( View& view )
{
   ResetSession();
   m_targetView = new View( view );
   m_targetView->AddToDynamicTargets();

   // Freeze the fill parameters and the seed for this session.
   m_session.Assign( m_instance );
   m_session.p_strokes.Clear();
   if ( m_session.p_randomSeed == 0 )
   {
      std::random_device rd;
      uint32 seed;
      do seed = rd(); while ( seed == 0 );
      m_session.p_randomSeed = seed;
   }
}

void RepatchInterface::ResetSession()
{
   if ( m_targetView != nullptr )
   {
      m_targetView->RemoveFromDynamicTargets();
      delete m_targetView, m_targetView = nullptr;
   }
   m_done.clear();
   m_undone.clear();
   m_path.Clear();
   m_onTarget = m_dragging = m_executing = false;
}

void RepatchInterface::RestoreOriginal()
{
   if ( m_targetView == nullptr || m_done.empty() )
      return;
   ImageVariant image = m_targetView->Image();
   Rect bounds = m_done.front().roi;
   for ( auto it = m_done.rbegin(); it != m_done.rend(); ++it )
   {
      RepatchInstance::PasteTile( image, it->roi, it->before );
      bounds.Unite( it->roi );
   }
   m_targetView->Window().RegenerateImageRect( bounds );
}

Rect RepatchInterface::PathBounds( const Array<DPoint>& path, int radius ) const
{
   if ( path.IsEmpty() )
      return Rect( 0 );
   DRect r( path[0], path[0] );
   for ( const DPoint& p : path )
   {
      r.x0 = Min( r.x0, p.x ); r.x1 = Max( r.x1, p.x );
      r.y0 = Min( r.y0, p.y ); r.y1 = Max( r.y1, p.y );
   }
   return Rect( int( Floor( r.x0 ) ) - radius - 2, int( Floor( r.y0 ) ) - radius - 2,
                int( Ceil( r.x1 ) ) + radius + 2, int( Ceil( r.y1 ) ) + radius + 2 );
}

void RepatchInterface::InvalidateCursor()
{
   if ( m_targetView == nullptr )
      return;
   ImageWindow w = m_targetView->Window();
   double d = double( m_instance.p_brushRadius ) + w.ViewportScalarToImage( w.DisplayPixelRatio() * 2 );
   w.UpdateImageRect( DRect( m_cursor - d, m_cursor + d ) );
}

void RepatchInterface::InvalidatePath()
{
   if ( m_targetView == nullptr || m_path.IsEmpty() )
      return;
   m_targetView->Window().UpdateImageRect( DRect( PathBounds( m_path, m_instance.p_brushRadius ) ) );
}

void RepatchInterface::AppendPathPoint( const DPoint& p )
{
   if ( m_path.IsEmpty() )
   {
      m_path.Add( p );
      return;
   }
   // By value: Array::Add() below can reallocate m_path, which would leave a
   // reference to its last element dangling (observed as strokes densified
   // from (0,0) toward the cursor).
   const DPoint last = *m_path.ReverseBegin();
   const double step = Max( 1.0, double( m_instance.p_brushRadius ) / 3.0 );
   const double dist = last.DistanceTo( p );
   if ( dist < step )
      return;
   const int n = int( dist / step );
   for ( int i = 1; i <= n; ++i )
      m_path.Add( last + ( p - last ) * ( double( i ) / n ) );
   if ( *m_path.ReverseBegin() != p )
      m_path.Add( p );
}

void RepatchInterface::DynamicMouseEnter( View& v )
{
   if ( !m_executing && SessionActive() && v == *m_targetView )
   {
      m_onTarget = true;
      InvalidateCursor();
   }
}

void RepatchInterface::DynamicMouseLeave( View& v )
{
   if ( !m_executing && SessionActive() && v == *m_targetView )
   {
      InvalidateCursor();
      m_onTarget = false;
   }
}

void RepatchInterface::DynamicMouseMove( View& v, const DPoint& p, unsigned /*buttons*/, unsigned /*modifiers*/ )
{
   if ( m_executing || !BrushMode() || !SessionActive() || v != *m_targetView )
      return;
   InvalidateCursor();
   m_cursor = p;
   InvalidateCursor();
   if ( m_dragging )
   {
      AppendPathPoint( p );
      InvalidatePath();
   }
}

void RepatchInterface::DynamicMousePress( View& v, const DPoint& p, int button, unsigned /*buttons*/, unsigned /*modifiers*/ )
{
   if ( m_executing || !BrushMode() || button != MouseButton::Left )
      return;
   if ( !SessionActive() )
   {
      if ( !v.IsMainView() )
      {
         Console().WarningLn( "<end><cbr>** Repatch: painting is only supported on main views, not previews." );
         return;
      }
      SelectTarget( v );
      m_onTarget = true;
      UpdateControls();
   }
   else if ( v != *m_targetView )
   {
      Console().WarningLn( "<end><cbr>** Repatch: the session target is " + String( m_targetView->FullId() )
                           + ". Press &#10004; or &#10008; before painting on another view." );
      return;
   }
   m_dragging = true;
   m_path.Clear();
   m_cursor = p;
   AppendPathPoint( p );
   InvalidatePath();
}

void RepatchInterface::DynamicMouseRelease( View& v, const DPoint& p, int button, unsigned /*buttons*/, unsigned /*modifiers*/ )
{
   if ( !m_dragging || button != MouseButton::Left )
      return;
   m_dragging = false;
   if ( !SessionActive() || v != *m_targetView )
   {
      m_path.Clear();
      return;
   }
   AppendPathPoint( p );
   InvalidatePath();
   CommitStroke();
   m_path.Clear();
   UpdateControls();
}

bool RepatchInterface::DynamicKeyPress( View& /*v*/, int key, unsigned modifiers )
{
   if ( !BrushMode() )
      return false;
   switch ( key )
   {
   case KeyCode::LeftBracket:
      SetBrushRadius( m_instance.p_brushRadius - ( ( modifiers & KeyModifier::Shift ) ? 10 : 1 ) );
      return true;
   case KeyCode::RightBracket:
      SetBrushRadius( m_instance.p_brushRadius + ( ( modifiers & KeyModifier::Shift ) ? 10 : 1 ) );
      return true;
   case KeyCode::Z:
      if ( modifiers & KeyModifier::Control )
      {
         UndoStroke();
         return true;
      }
      return false;
   case KeyCode::Y:
      if ( modifiers & KeyModifier::Control )
      {
         RedoStroke();
         return true;
      }
      return false;
   default:
      return false;
   }
}

bool RepatchInterface::RequiresDynamicUpdate( const View& v, const DRect& updateRect ) const
{
   if ( !BrushMode() || !SessionActive() || v != *m_targetView )
      return false;
   const double r = double( m_instance.p_brushRadius ) + 2;
   if ( m_onTarget && DRect( m_cursor - r, m_cursor + r ).Intersects( updateRect ) )
      return true;
   if ( m_dragging && !m_path.IsEmpty() && DRect( PathBounds( m_path, m_instance.p_brushRadius ) ).Intersects( updateRect ) )
      return true;
   return false;
}

void RepatchInterface::DynamicPaint( const View& v, VectorGraphics& g, const DRect& /*updateRect*/ ) const
{
   if ( !BrushMode() || !SessionActive() || v != *m_targetView )
      return;

   ImageWindow window = v.Window();
   g.EnableAntialiasing();
   g.SetBrush( Brush::Null() );

   const double radius = window.ImageScalarToViewport( double( m_instance.p_brushRadius ) );

   if ( m_dragging && m_path.Length() > 1 )
   {
      // Swept region: the path drawn with a translucent pen as wide as the brush.
      Array<DPoint> pts;
      for ( const DPoint& p : m_path )
         pts.Add( window.ImageToViewport( p ) );
      g.SetPen( Pen( 0x60FF8000, float( 2 * radius ), PenStyle::Solid, PenCap::Round, PenJoin::Round ) );
      g.DrawPolyline( pts );
   }

   if ( m_onTarget )
   {
      g.SetCompositionOperator( CompositionOp::Difference );
      g.SetPen( 0xFFFFFFFF, window.DisplayPixelRatio() );
      if ( radius >= 1 )
      {
         DPoint c = window.ImageToViewport( m_cursor );
         g.DrawCircle( c.x, c.y, radius );
      }
   }
}

// ----------------------------------------------------------------------------
// Session editing
// ----------------------------------------------------------------------------

bool RepatchInterface::CommitStroke()
{
   if ( m_targetView == nullptr || m_path.IsEmpty() )
      return false;

   repatch::Stroke stroke;
   stroke.radius = m_instance.p_brushRadius;
   stroke.softness = m_instance.p_brushSoftness;
   stroke.opacity = m_instance.p_brushOpacity;
   // Quantize to 2 decimal places at capture time -- this is the precision
   // RPStrokeX/RPStrokeY serialize at (RepatchParameters.cpp), so the live
   // fill below uses exactly the numbers that will end up in a saved
   // project, a generated script, or a process icon. Without this, only an
   // in-memory instance copy reproduces the pixels seen while painting; any
   // round trip through the serialized strokes table would quietly shift
   // the coordinates and the fill could differ by a small amount.
   for ( const DPoint& p : m_path )
   {
      float rx = std::round( float( p.x ) * 100.0f ) / 100.0f;
      float ry = std::round( float( p.y ) * 100.0f ) / 100.0f;
      stroke.points.push_back( { rx, ry } );
   }

   ImageVariant image = m_targetView->Image();
   repatch::FillParams params = m_session.CoreParams();
   params.sampleRing = repatch::EffectiveSampleRing( params.sampleRing, stroke.radius );
   repatch::BBox roi = repatch::StrokeRoi( stroke, image.Width(), image.Height(), params );
   if ( roi.Empty() )
   {
      Console().WarningLn( "<end><cbr>** Repatch: the stroke lies outside the image; ignored." );
      return false;
   }
   const Rect r( roi.x0, roi.y0, roi.x1, roi.y1 );

   SessionStroke s;
   s.stroke = stroke;
   s.roi = r;

   // Both tile captures and the fill itself are covered by the same
   // try/catch: if the 'after' capture throws (e.g. allocation failure)
   // right after FillStroke has already modified the image, the pixels
   // would otherwise be left permanently changed with no undo tile pushed
   // to m_done -- invisible to Undo/Clear and absent from the exported
   // strokes table. Treat that the same as a failure of FillStroke itself:
   // paste 'before' back so the view matches "this stroke was not
   // committed". If 'before' itself failed to capture, the view was never
   // touched, so there is nothing to restore.
   bool haveBefore = false;
   try
   {
      s.before = RepatchInstance::CopyTile( image, r );
      haveBefore = true;

      Rect modified = m_session.FillStroke( image, stroke, repatch::DeriveStrokeSeed( m_session.p_randomSeed, int( m_done.size() ) ) );
      if ( modified != r )
         throw Error( "Internal error: stroke region mismatch." );

      s.after = RepatchInstance::CopyTile( image, r );
   }
   catch ( Exception& x )
   {
      // Leave the image exactly as it was and report on the console.
      if ( haveBefore )
      {
         RepatchInstance::PasteTile( image, r, s.before );
         m_targetView->Window().RegenerateImageRect( r );
      }
      Console().CriticalLn( "<end><cbr>*** Repatch: " + x.Message() );
      return false;
   }
   catch ( ... )
   {
      if ( haveBefore )
      {
         RepatchInstance::PasteTile( image, r, s.before );
         m_targetView->Window().RegenerateImageRect( r );
      }
      Console().CriticalLn( "<end><cbr>*** Repatch: unexpected error while filling a stroke." );
      return false;
   }

   m_done.push_back( std::move( s ) );
   m_undone.clear();
   m_targetView->Window().RegenerateImageRect( r );
   return true;
}

void RepatchInterface::UndoStroke()
{
   if ( m_targetView == nullptr || m_done.empty() )
      return;
   ImageVariant image = m_targetView->Image();
   SessionStroke s = std::move( m_done.back() );
   m_done.pop_back();
   RepatchInstance::PasteTile( image, s.roi, s.before );
   m_targetView->Window().RegenerateImageRect( s.roi );
   m_undone.push_back( std::move( s ) );
   UpdateControls();
}

void RepatchInterface::RedoStroke()
{
   if ( m_targetView == nullptr || m_undone.empty() )
      return;
   ImageVariant image = m_targetView->Image();
   SessionStroke s = std::move( m_undone.back() );
   m_undone.pop_back();
   RepatchInstance::PasteTile( image, s.roi, s.after );
   m_targetView->Window().RegenerateImageRect( s.roi );
   m_done.push_back( std::move( s ) );
   UpdateControls();
}

void RepatchInterface::ClearStrokes()
{
   while ( !m_done.empty() )
      UndoStroke();
   m_undone.clear();
   UpdateControls();
}

void RepatchInterface::ExportStrokes( RepatchInstance& instance ) const
{
   instance.p_maskSource = RPMaskSource::Brush;
   instance.p_strokes.Clear();
   int32 index = 0;
   for ( const SessionStroke& s : m_done )
   {
      for ( const repatch::StrokePoint& p : s.stroke.points )
      {
         RepatchInstance::StrokeRow row;
         row.strokeIndex = index;
         row.x = p.x;
         row.y = p.y;
         row.radius = s.stroke.radius;
         row.softness = s.stroke.softness;
         row.opacity = s.stroke.opacity;
         instance.p_strokes.Add( row );
      }
      ++index;
   }
}

void RepatchInterface::SetBrushRadius( int r )
{
   r = Range( r, int( TheRPBrushRadiusParameter->MinimumValue() ), int( TheRPBrushRadiusParameter->MaximumValue() ) );
   if ( r == m_instance.p_brushRadius )
      return;
   InvalidateCursor();
   m_instance.p_brushRadius = r;
   InvalidateCursor();
   if ( GUI != nullptr )
   {
      GUI->BrushRadius_SpinBox.SetValue( r );
      GUI->BrushPreview_Control.Update();
   }
}

// ----------------------------------------------------------------------------
// Controls
// ----------------------------------------------------------------------------

void RepatchInterface::UpdateControls()
{
   GUI->Patch_RadioButton.SetChecked( m_instance.p_mode == RPMode::Patch );
   GUI->Neural_RadioButton.SetChecked( m_instance.p_mode == RPMode::Neural );

   GUI->Brush_RadioButton.SetChecked( BrushMode() );
   GUI->MaskImage_RadioButton.SetChecked( !BrushMode() );

   GUI->BrushRadius_SpinBox.SetValue( m_instance.p_brushRadius );
   GUI->BrushSoftness_NumericControl.SetValue( m_instance.p_brushSoftness );
   GUI->BrushOpacity_NumericControl.SetValue( m_instance.p_brushOpacity );
   GUI->BrushPreview_Control.Update();

   View maskView = m_instance.p_maskId.IsEmpty() ? View::Null() : View::ViewById( IsoString( m_instance.p_maskId ) );
   GUI->Mask_ViewList.SelectView( maskView );
   GUI->MaskThreshold_NumericControl.SetValue( m_instance.p_maskThreshold );

   GUI->PatchSize_SpinBox.SetValue( m_instance.p_patchSize );
   GUI->PyramidLevelsAuto_CheckBox.SetChecked( m_instance.p_pyramidLevelsAuto );
   GUI->PyramidLevels_SpinBox.SetValue( m_instance.p_pyramidLevels );
   GUI->PyramidLevels_SpinBox.Enable( !m_instance.p_pyramidLevelsAuto );
   GUI->Iterations_SpinBox.SetValue( m_instance.p_iterations );

   GUI->SearchRadius_SpinBox.SetValue( m_instance.p_searchRadius );
   GUI->SampleRing_SpinBox.SetValue( m_instance.p_sampleRing );

   GUI->MatchSpace_ComboBox.SetCurrentItem( m_instance.p_matchSpace );
   GUI->Feather_SpinBox.SetValue( m_instance.p_feather );
   GUI->RandomSeed_NumericEdit.SetValue( m_instance.p_randomSeed );

   UpdateModeControls();
   UpdateTargetInfo();
}

void RepatchInterface::UpdateModeControls()
{
   const bool brush = BrushMode();

   GUI->Brush_Control.Enable( brush );
   GUI->Mask_Control.Enable( !brush );

   // Fill parameters are frozen for the entire duration of a painting
   // session (from the first mouse press, in SelectTarget), not just while
   // it has committed strokes -- undoing back to zero strokes must not
   // silently re-enable editing of parameters that CommitStroke() will keep
   // reading from the already-frozen m_session.
   const bool sessionActive = SessionActive();
   GUI->Patch_Control.Enable( !sessionActive );
   GUI->Sampling_Control.Enable( !sessionActive );
   GUI->Output_Control.Enable( !sessionActive );

   GUI->UndoStroke_PushButton.Enable( brush && !m_done.empty() );
   GUI->RedoStroke_PushButton.Enable( brush && !m_undone.empty() );
   GUI->ClearStrokes_PushButton.Enable( brush && !m_done.empty() );
}

void RepatchInterface::UpdateTargetInfo()
{
   if ( !SessionActive() )
      GUI->Target_Label.SetText( "No target view selected" );
   else
      GUI->Target_Label.SetText( String().Format( "Target: %s (%u stroke(s))",
                                                  IsoString( m_targetView->FullId() ).c_str(), unsigned( m_done.size() ) ) );
}

// ----------------------------------------------------------------------------

void RepatchInterface::e_ModeClicked( Button& sender, bool /*checked*/ )
{
   if ( sender == GUI->Patch_RadioButton )
      m_instance.p_mode = RPMode::Patch;
   else if ( sender == GUI->Neural_RadioButton )
      m_instance.p_mode = RPMode::Neural;
}

void RepatchInterface::e_MaskSourceClicked( Button& sender, bool /*checked*/ )
{
   pcl_enum source = ( sender == GUI->Brush_RadioButton ) ? RPMaskSource::Brush : RPMaskSource::MaskImage;
   if ( source == m_instance.p_maskSource )
      return;
   if ( SessionActive() && !m_done.empty() )
   {
      if ( MessageBox( "<p>Switching the mask source discards the current brush strokes. Continue?</p>",
                       "Repatch", StdIcon::Warning, StdButton::No, StdButton::Yes ).Execute() != StdButton::Yes )
      {
         UpdateControls();
         return;
      }
   }
   RestoreOriginal();
   ResetSession();
   m_instance.p_maskSource = source;
   UpdateControls();
}

void RepatchInterface::e_ViewSelected( ViewList& sender, View& view )
{
   if ( sender == GUI->Mask_ViewList )
      m_instance.p_maskId = view.IsNull() ? String() : String( view.FullId() );
}

void RepatchInterface::e_RealValueUpdated( NumericEdit& sender, double value )
{
   if ( sender == GUI->MaskThreshold_NumericControl )
      m_instance.p_maskThreshold = float( value );
   else if ( sender == GUI->RandomSeed_NumericEdit )
      m_instance.p_randomSeed = uint32( value );
   else if ( sender == GUI->BrushSoftness_NumericControl )
   {
      m_instance.p_brushSoftness = float( value );
      GUI->BrushPreview_Control.Update();
   }
   else if ( sender == GUI->BrushOpacity_NumericControl )
   {
      m_instance.p_brushOpacity = float( value );
      GUI->BrushPreview_Control.Update();
   }
}

void RepatchInterface::e_IntegerValueUpdated( SpinBox& sender, int value )
{
   if ( sender == GUI->PatchSize_SpinBox )
   {
      if ( (value & 1) == 0 ) // keep patch sizes odd
      {
         ++value;
         sender.SetValue( value );
      }
      m_instance.p_patchSize = value;
   }
   else if ( sender == GUI->PyramidLevels_SpinBox )
      m_instance.p_pyramidLevels = value;
   else if ( sender == GUI->Iterations_SpinBox )
      m_instance.p_iterations = value;
   else if ( sender == GUI->SearchRadius_SpinBox )
      m_instance.p_searchRadius = value;
   else if ( sender == GUI->SampleRing_SpinBox )
      m_instance.p_sampleRing = value;
   else if ( sender == GUI->Feather_SpinBox )
      m_instance.p_feather = value;
   else if ( sender == GUI->BrushRadius_SpinBox )
      SetBrushRadius( value );
}

void RepatchInterface::e_CheckClicked( Button& sender, bool checked )
{
   if ( sender == GUI->PyramidLevelsAuto_CheckBox )
   {
      m_instance.p_pyramidLevelsAuto = checked;
      GUI->PyramidLevels_SpinBox.Enable( !checked );
   }
}

void RepatchInterface::e_ItemSelected( ComboBox& sender, int itemIndex )
{
   if ( sender == GUI->MatchSpace_ComboBox )
      m_instance.p_matchSpace = itemIndex;
}

void RepatchInterface::e_StrokeButtonClicked( Button& sender, bool /*checked*/ )
{
   if ( sender == GUI->UndoStroke_PushButton )
      UndoStroke();
   else if ( sender == GUI->RedoStroke_PushButton )
      RedoStroke();
   else if ( sender == GUI->ClearStrokes_PushButton )
      ClearStrokes();
}

void RepatchInterface::e_BrushPreviewPaint( Control& sender, const Rect& /*updateRect*/ )
{
   // Render the weight profile normalized to the radius (independent of size).
   const int n = 65;
   const int radius = m_instance.p_brushRadius;
   const float softness = m_instance.p_brushSoftness;
   const float opacity = m_instance.p_brushOpacity;
   Bitmap bmp( n, n, BitmapFormat::RGB32 );
   const double c = ( n - 1 ) / 2.0;
   for ( int y = 0; y < n; ++y )
      for ( int x = 0; x < n; ++x )
      {
         double d = Sqrt( ( x - c ) * ( x - c ) + ( y - c ) * ( y - c ) ) / c; // 0..~1.41 in radius units
         float w = repatch::BrushProfile( float( d * radius ), radius, softness ) * opacity;
         int v = RoundInt( w * 255 );
         bmp.SetPixel( x, y, RGBAColor( v, v, v ) );
      }
   Graphics g( sender );
   g.DisableSmoothInterpolation();
   g.DrawScaledBitmap( sender.BoundsRect(), bmp );
}

// ----------------------------------------------------------------------------

RepatchInterface::GUIData::GUIData( RepatchInterface& w )
{
   pcl::Font font = w.Font();
   int labelWidth1 = font.Width( String( "Pyramid levels:" ) + 'M' );
   int editWidth1 = font.Width( String( '0', 8 ) );

   auto setLabel = [&]( Label& label, const char* text ) {
      label.SetText( text );
      label.SetMinWidth( labelWidth1 );
      label.SetTextAlignment( TextAlign::Right|TextAlign::VertCenter );
   };
   auto setSpinBox = [&]( SpinBox& box, const MetaInt32* p, const char* toolTip ) {
      box.SetRange( int( p->MinimumValue() ), int( p->MaximumValue() ) );
      box.SetFixedWidth( editWidth1 );
      box.SetToolTip( toolTip );
      box.OnValueUpdated( (SpinBox::value_event_handler)&RepatchInterface::e_IntegerValueUpdated, w );
   };
   auto setNumeric = [&]( NumericControl& n, const char* text, const MetaFloat* p, const char* toolTip ) {
      n.label.SetText( text );
      n.label.SetFixedWidth( labelWidth1 );
      n.slider.SetScaledMinWidth( 160 );
      n.slider.SetRange( 0, 100 );
      n.SetReal();
      n.SetRange( p->MinimumValue(), p->MaximumValue() );
      n.SetPrecision( p->Precision() );
      n.edit.SetFixedWidth( editWidth1 );
      n.SetToolTip( toolTip );
      n.OnValueUpdated( (NumericEdit::value_event_handler)&RepatchInterface::e_RealValueUpdated, w );
   };

   //
   // Mode
   //

   Patch_RadioButton.SetText( "Patch" );
   Patch_RadioButton.SetToolTip( "<p>Classic PatchMatch content-aware fill: the hole is synthesized from patches of the surrounding image.</p>" );
   Patch_RadioButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_ModeClicked, w );

   Neural_RadioButton.SetText( "Neural" );
   Neural_RadioButton.SetToolTip( "<p>Neural (ONNX) inpainting mode &mdash; coming in a later release.</p>" );
   Neural_RadioButton.Disable();
   Neural_RadioButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_ModeClicked, w );

   Mode_Sizer.SetSpacing( 8 );
   Mode_Sizer.Add( Patch_RadioButton );
   Mode_Sizer.Add( Neural_RadioButton );
   Mode_Sizer.AddStretch();

   Mode_Control.SetSizer( Mode_Sizer );
   Mode_SectionBar.SetTitle( "Mode" );
   Mode_SectionBar.SetSection( Mode_Control );

   //
   // Mask source
   //

   Brush_RadioButton.SetText( "Brush" );
   Brush_RadioButton.SetToolTip( "<p>Paint the region to fill directly on an image. Click an image to make it the target, "
                                 "then press and drag; each stroke is filled when you release the mouse button. "
                                 "Press &#10004; to commit the session to the image's history, &#10008; to discard it.</p>" );
   Brush_RadioButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_MaskSourceClicked, w );

   MaskImage_RadioButton.SetText( "Mask image" );
   MaskImage_RadioButton.SetToolTip( "<p>Take the region to fill from a separate mask image (see the Mask section). "
                                     "Press &#10004; to apply to the current view.</p>" );
   MaskImage_RadioButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_MaskSourceClicked, w );

   MaskSource_Sizer.SetSpacing( 8 );
   MaskSource_Sizer.Add( Brush_RadioButton );
   MaskSource_Sizer.Add( MaskImage_RadioButton );
   MaskSource_Sizer.AddStretch();

   MaskSource_Control.SetSizer( MaskSource_Sizer );
   MaskSource_SectionBar.SetTitle( "Mask source" );
   MaskSource_SectionBar.SetSection( MaskSource_Control );

   //
   // Brush
   //

   setLabel( BrushRadius_Label, "Radius:" );
   setSpinBox( BrushRadius_SpinBox, TheRPBrushRadiusParameter,
               "<p>Brush radius in pixels. Keyboard: [ and ] change it by 1, with Shift by 10.</p>" );

   BrushRadius_Sizer.SetSpacing( 4 );
   BrushRadius_Sizer.Add( BrushRadius_Label );
   BrushRadius_Sizer.Add( BrushRadius_SpinBox );
   BrushRadius_Sizer.AddStretch();

   setNumeric( BrushSoftness_NumericControl, "Softness:", TheRPBrushSoftnessParameter,
               "<p>Edge softness. 0 replaces the whole disc; higher values fade the fill into the original "
               "over the outer part of the radius (1 = over the whole radius).</p>" );
   setNumeric( BrushOpacity_NumericControl, "Opacity:", TheRPBrushOpacityParameter,
               "<p>How much of the synthesized content is mixed in: 1 replaces, 0.5 blends half and half.</p>" );

   UndoStroke_PushButton.SetText( "Undo stroke" );
   UndoStroke_PushButton.SetToolTip( "<p>Undo the last stroke (Ctrl+Z).</p>" );
   UndoStroke_PushButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_StrokeButtonClicked, w );

   RedoStroke_PushButton.SetText( "Redo" );
   RedoStroke_PushButton.SetToolTip( "<p>Redo the last undone stroke (Ctrl+Y).</p>" );
   RedoStroke_PushButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_StrokeButtonClicked, w );

   ClearStrokes_PushButton.SetText( "Clear" );
   ClearStrokes_PushButton.SetToolTip( "<p>Undo all strokes of this session.</p>" );
   ClearStrokes_PushButton.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_StrokeButtonClicked, w );

   BrushActions_Sizer.SetSpacing( 4 );
   BrushActions_Sizer.AddUnscaledSpacing( labelWidth1 + w.LogicalPixelsToPhysical( 4 ) );
   BrushActions_Sizer.Add( UndoStroke_PushButton );
   BrushActions_Sizer.Add( RedoStroke_PushButton );
   BrushActions_Sizer.Add( ClearStrokes_PushButton );
   BrushActions_Sizer.AddStretch();

   Target_Label.SetText( "No target view selected" );
   Target_Label.SetTextAlignment( TextAlign::Left|TextAlign::VertCenter );

   BrushParameters_Sizer.SetSpacing( 4 );
   BrushParameters_Sizer.Add( BrushRadius_Sizer );
   BrushParameters_Sizer.Add( BrushSoftness_NumericControl );
   BrushParameters_Sizer.Add( BrushOpacity_NumericControl );
   BrushParameters_Sizer.Add( BrushActions_Sizer );
   BrushParameters_Sizer.Add( Target_Label );

   BrushPreview_Control.SetScaledFixedSize( 80, 80 );
   BrushPreview_Control.SetToolTip( "<p>Brush profile: white = fully replaced, black = untouched.</p>" );
   BrushPreview_Control.OnPaint( (Control::paint_event_handler)&RepatchInterface::e_BrushPreviewPaint, w );

   Brush_Sizer.SetSpacing( 8 );
   Brush_Sizer.Add( BrushParameters_Sizer, 100 );
   Brush_Sizer.Add( BrushPreview_Control );

   Brush_Control.SetSizer( Brush_Sizer );
   Brush_SectionBar.SetTitle( "Brush" );
   Brush_SectionBar.SetSection( Brush_Control );

   //
   // Mask
   //

   setLabel( MaskView_Label, "Mask image:" );

   Mask_ViewList.Regenerate( true, false ); // main views only
   Mask_ViewList.SetToolTip( "<p>Image whose pixels above the threshold mark the region to fill. It must have the "
                             "dimensions of the target image, or of the main image when the target is a preview "
                             "(the mask is then cropped automatically).</p>" );
   Mask_ViewList.OnViewSelected( (ViewList::view_event_handler)&RepatchInterface::e_ViewSelected, w );

   MaskView_Sizer.SetSpacing( 4 );
   MaskView_Sizer.Add( MaskView_Label );
   MaskView_Sizer.Add( Mask_ViewList, 100 );

   setNumeric( MaskThreshold_NumericControl, "Threshold:", TheRPMaskThresholdParameter,
               "<p>Mask pixels with a value strictly greater than this threshold are filled.</p>" );
   MaskThreshold_NumericControl.slider.SetRange( 0, 1000 );

   Mask_Sizer.SetSpacing( 4 );
   Mask_Sizer.Add( MaskView_Sizer );
   Mask_Sizer.Add( MaskThreshold_NumericControl );

   Mask_Control.SetSizer( Mask_Sizer );
   Mask_SectionBar.SetTitle( "Mask" );
   Mask_SectionBar.SetSection( Mask_Control );

   //
   // Patch
   //

   setLabel( PatchSize_Label, "Patch size:" );
   setSpinBox( PatchSize_SpinBox, TheRPPatchSizeParameter,
               "<p>Side of the square patches used for matching, in pixels (odd, 5&ndash;41). Larger patches "
               "preserve structure better; smaller patches follow fine texture. Use at least the size of the "
               "largest star you want reproduced.</p>" );
   PatchSize_SpinBox.SetStepSize( 2 );

   PatchSize_Sizer.SetSpacing( 4 );
   PatchSize_Sizer.Add( PatchSize_Label );
   PatchSize_Sizer.Add( PatchSize_SpinBox );
   PatchSize_Sizer.AddStretch();

   setLabel( PyramidLevels_Label, "Pyramid levels:" );
   setSpinBox( PyramidLevels_SpinBox, TheRPPyramidLevelsParameter,
               "<p>Number of coarse-to-fine resolution levels (1&ndash;8). Only used when Auto is unchecked.</p>" );

   PyramidLevelsAuto_CheckBox.SetText( "Auto" );
   PyramidLevelsAuto_CheckBox.SetToolTip( "<p>Choose the number of pyramid levels automatically as "
                                          "1 + log2(hole size / patch size), so the coarsest level sees the hole as "
                                          "roughly one patch.</p>" );
   PyramidLevelsAuto_CheckBox.OnClick( (pcl::Button::click_event_handler)&RepatchInterface::e_CheckClicked, w );

   PyramidLevels_Sizer.SetSpacing( 4 );
   PyramidLevels_Sizer.Add( PyramidLevels_Label );
   PyramidLevels_Sizer.Add( PyramidLevels_SpinBox );
   PyramidLevels_Sizer.Add( PyramidLevelsAuto_CheckBox );
   PyramidLevels_Sizer.AddStretch();

   setLabel( Iterations_Label, "Iterations:" );
   setSpinBox( Iterations_SpinBox, TheRPIterationsParameter,
               "<p>PatchMatch propagation/search passes per pyramid level (1&ndash;50). 4&ndash;6 is usually enough.</p>" );

   Iterations_Sizer.SetSpacing( 4 );
   Iterations_Sizer.Add( Iterations_Label );
   Iterations_Sizer.Add( Iterations_SpinBox );
   Iterations_Sizer.AddStretch();

   Patch_Sizer.SetSpacing( 4 );
   Patch_Sizer.Add( PatchSize_Sizer );
   Patch_Sizer.Add( PyramidLevels_Sizer );
   Patch_Sizer.Add( Iterations_Sizer );

   Patch_Control.SetSizer( Patch_Sizer );
   Patch_SectionBar.SetTitle( "Patch" );
   Patch_SectionBar.SetSection( Patch_Control );

   //
   // Sampling
   //

   setLabel( SearchRadius_Label, "Search radius:" );
   setSpinBox( SearchRadius_SpinBox, TheRPSearchRadiusParameter,
               "<p>Maximum distance in pixels between a hole pixel and the source patches used to fill it. "
               "0 searches the whole image.</p>" );

   SearchRadius_Sizer.SetSpacing( 4 );
   SearchRadius_Sizer.Add( SearchRadius_Label );
   SearchRadius_Sizer.Add( SearchRadius_SpinBox );
   SearchRadius_Sizer.AddStretch();

   setLabel( SampleRing_Label, "Sample ring:" );
   setSpinBox( SampleRing_SpinBox, TheRPSampleRingParameter,
               "<p>If greater than 0, source patches are taken only from a ring of this width in pixels around "
               "the hole. Recommended for astronomical images. 0 uses the whole image in Mask image mode; in "
               "Brush mode 0 means an automatic ring of max(32, 3 &times; radius) pixels around each stroke.</p>" );

   SampleRing_Sizer.SetSpacing( 4 );
   SampleRing_Sizer.Add( SampleRing_Label );
   SampleRing_Sizer.Add( SampleRing_SpinBox );
   SampleRing_Sizer.AddStretch();

   Sampling_Sizer.SetSpacing( 4 );
   Sampling_Sizer.Add( SearchRadius_Sizer );
   Sampling_Sizer.Add( SampleRing_Sizer );

   Sampling_Control.SetSizer( Sampling_Sizer );
   Sampling_SectionBar.SetTitle( "Sampling" );
   Sampling_SectionBar.SetSection( Sampling_Control );

   //
   // Output
   //

   setLabel( MatchSpace_Label, "Match space:" );

   MatchSpace_ComboBox.AddItem( "Linear" );
   MatchSpace_ComboBox.AddItem( "Stretched" );
   MatchSpace_ComboBox.SetToolTip( "<p>Stretched (recommended for linear data) compares patches on an automatically "
                                   "stretched copy of the image, while the filled pixels are always copied from the "
                                   "original linear data. Linear compares raw pixel values.</p>" );
   MatchSpace_ComboBox.OnItemSelected( (ComboBox::item_event_handler)&RepatchInterface::e_ItemSelected, w );

   MatchSpace_Sizer.SetSpacing( 4 );
   MatchSpace_Sizer.Add( MatchSpace_Label );
   MatchSpace_Sizer.Add( MatchSpace_ComboBox );
   MatchSpace_Sizer.AddStretch();

   setLabel( Feather_Label, "Feather:" );
   setSpinBox( Feather_SpinBox, TheRPFeatherParameter,
               "<p>Width in pixels of the soft blend between the synthesized region and the original image just "
               "outside the hole. 0 disables blending.</p>" );

   Feather_Sizer.SetSpacing( 4 );
   Feather_Sizer.Add( Feather_Label );
   Feather_Sizer.Add( Feather_SpinBox );
   Feather_Sizer.AddStretch();

   RandomSeed_NumericEdit.label.SetText( "Random seed:" );
   RandomSeed_NumericEdit.label.SetFixedWidth( labelWidth1 );
   RandomSeed_NumericEdit.SetInteger();
   RandomSeed_NumericEdit.SetRange( TheRPRandomSeedParameter->MinimumValue(), TheRPRandomSeedParameter->MaximumValue() );
   RandomSeed_NumericEdit.edit.SetFixedWidth( editWidth1 );
   RandomSeed_NumericEdit.SetToolTip( "<p>Seed of the random number generator. A non-zero seed makes the result "
                                      "reproducible; 0 draws a new seed for every run or painting session "
                                      "(the seed used is stored in the history record).</p>" );
   RandomSeed_NumericEdit.OnValueUpdated( (NumericEdit::value_event_handler)&RepatchInterface::e_RealValueUpdated, w );

   // NumericEdit's own sizer has no stretch, so on its own it would spread the
   // label and the edit box across the row; wrap it like the rows above.
   RandomSeed_Sizer.Add( RandomSeed_NumericEdit );
   RandomSeed_Sizer.AddStretch();

   Output_Sizer.SetSpacing( 4 );
   Output_Sizer.Add( MatchSpace_Sizer );
   Output_Sizer.Add( Feather_Sizer );
   Output_Sizer.Add( RandomSeed_Sizer );

   Output_Control.SetSizer( Output_Sizer );
   Output_SectionBar.SetTitle( "Output" );
   Output_SectionBar.SetSection( Output_Control );

   //
   // Global
   //

   Global_Sizer.SetMargin( 8 );
   Global_Sizer.SetSpacing( 6 );
   Global_Sizer.Add( Mode_SectionBar );
   Global_Sizer.Add( Mode_Control );
   Global_Sizer.Add( MaskSource_SectionBar );
   Global_Sizer.Add( MaskSource_Control );
   Global_Sizer.Add( Brush_SectionBar );
   Global_Sizer.Add( Brush_Control );
   Global_Sizer.Add( Mask_SectionBar );
   Global_Sizer.Add( Mask_Control );
   Global_Sizer.Add( Patch_SectionBar );
   Global_Sizer.Add( Patch_Control );
   Global_Sizer.Add( Sampling_SectionBar );
   Global_Sizer.Add( Sampling_Control );
   Global_Sizer.Add( Output_SectionBar );
   Global_Sizer.Add( Output_Control );

   w.SetSizer( Global_Sizer );

   w.EnsureLayoutUpdated();
   w.AdjustToContents();
   w.SetFixedSize();
}

// ----------------------------------------------------------------------------

} // pcl
