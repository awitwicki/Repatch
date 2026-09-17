#ifndef __RepatchModule_h
#define __RepatchModule_h

#include <pcl/MetaModule.h>

namespace pcl
{

class RepatchModule : public MetaModule
{
public:

   RepatchModule();

   const char* Version() const override;
   IsoString Name() const override;
   String Description() const override;
   String Company() const override;
   String Author() const override;
   String Copyright() const override;
   String TradeMarks() const override;
   String OriginalFileName() const override;
   void GetReleaseDate( int& year, int& month, int& day ) const override;
};

} // pcl

#endif   // __RepatchModule_h
