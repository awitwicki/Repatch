#ifndef __RepatchProcess_h
#define __RepatchProcess_h

#include <pcl/MetaProcess.h>

namespace pcl
{

class RepatchProcess : public MetaProcess
{
public:

   RepatchProcess();

   IsoString Id() const override;
   IsoString Categories() const override;
   uint32 Version() const override;
   String Description() const override;
   IsoString IconImageSVG() const override;
   ProcessInterface* DefaultInterface() const override;
   ProcessImplementation* Create() const override;
   ProcessImplementation* Clone( const ProcessImplementation& ) const override;
};

PCL_BEGIN_LOCAL
extern RepatchProcess* TheRepatchProcess;
PCL_END_LOCAL

} // pcl

#endif   // __RepatchProcess_h
