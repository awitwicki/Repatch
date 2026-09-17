#pragma once
#include "FillBackend.h"
#include "PatchMatch.h"

namespace repatch {

class PatchBackend final : public FillBackend {
public:
   // Level-0 state captured after a successful Fill when debug capture is on.
   // Coordinates are relative to the working region (roiX0, roiY0).
   struct DebugInfo {
      int width = 0, height = 0, r = 0, roiX0 = 0, roiY0 = 0;
      Nnf nnf;
      Mask hole, target, valid;
   };

   const char* Name() const override { return "Patch"; }
   FillResult Fill( FillRequest& request, const ProgressFn& progress = {} ) override;

   void EnableDebugCapture( bool on = true ) { m_debug = on; }
   const DebugInfo& Debug() const { return m_debugInfo; }

private:
   bool m_debug = false;
   DebugInfo m_debugInfo;
};

} // namespace repatch
