#define MODULE_VERSION_MAJOR     0
#define MODULE_VERSION_MINOR     1
#define MODULE_VERSION_REVISION  0
#define MODULE_VERSION_BUILD     1
#define MODULE_VERSION_LANGUAGE  eng

#define MODULE_RELEASE_YEAR      2026
#define MODULE_RELEASE_MONTH     9
#define MODULE_RELEASE_DAY       17

#include "RepatchModule.h"
#include "RepatchProcess.h"
#include "RepatchInterface.h"

namespace pcl
{

RepatchModule::RepatchModule()
{
}

const char* RepatchModule::Version() const
{
   return PCL_MODULE_VERSION( MODULE_VERSION_MAJOR,
                              MODULE_VERSION_MINOR,
                              MODULE_VERSION_REVISION,
                              MODULE_VERSION_BUILD,
                              MODULE_VERSION_LANGUAGE );
}

IsoString RepatchModule::Name() const
{
   return "Repatch";
}

String RepatchModule::Description() const
{
   return "Repatch - Content-aware fill (PatchMatch) for PixInsight";
}

String RepatchModule::Company() const
{
   return String();
}

String RepatchModule::Author() const
{
   return "A. Witwicki";
}

String RepatchModule::Copyright() const
{
   return "Copyright (c) 2026 A. Witwicki";
}

String RepatchModule::TradeMarks() const
{
   return "PixInsight";
}

String RepatchModule::OriginalFileName() const
{
#ifdef __PCL_FREEBSD
   return "Repatch-pxm.so";
#endif
#ifdef __PCL_LINUX
   return "Repatch-pxm.so";
#endif
#ifdef __PCL_MACOSX
   return "Repatch-pxm.dylib";
#endif
#ifdef __PCL_WINDOWS
   return "Repatch-pxm.dll";
#endif
}

void RepatchModule::GetReleaseDate( int& year, int& month, int& day ) const
{
   year  = MODULE_RELEASE_YEAR;
   month = MODULE_RELEASE_MONTH;
   day   = MODULE_RELEASE_DAY;
}

} // pcl

// ----------------------------------------------------------------------------

PCL_MODULE_EXPORT int InstallPixInsightModule( int mode )
{
   new pcl::RepatchModule;

   if ( mode == pcl::InstallMode::FullInstall )
   {
      new pcl::RepatchProcess;
      new pcl::RepatchInterface;
   }

   return 0;
}
