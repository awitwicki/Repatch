cmake_minimum_required(VERSION 3.20)
project(PCLStatic LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Builds PCL-pxi.lib for Windows x64.
#
# PCL's git repository ships windows/vc17 MSBuild projects for the six
# third-party libraries but NOT for the main library: src/pcl/windows/ does
# not exist in the repo (only in an installed PixInsight, which carries
# whatever PCL version that core was built from). Building the pinned PCL
# commit on Windows therefore needs this project.
#
# Every setting below is transcribed from the Release|x64 configuration of
# src/pcl/windows/vc17/PCL.vcxproj as shipped with PixInsight 1.9.5:
#
#   Optimization         MaxSpeed                     -> /O2
#   InlineFunctionExpansion AnySuitable               -> /Ob2
#   IntrinsicFunctions   true                         -> /Oi
#   FavorSizeOrSpeed     Speed                        -> /Ot
#   OmitFramePointers    true                         -> /Oy
#   StringPooling        true                         -> /GF
#   ExceptionHandling    Async                        -> /EHa
#   RuntimeLibrary       MultiThreadedDLL             -> /MD
#   BufferSecurityCheck  false                        -> /GS-
#   FunctionLevelLinking false                        -> (no /Gy)
#   FloatingPointModel   Fast                         -> /fp:fast
#   RuntimeTypeInfo      true                         -> /GR
#   EnableEnhancedInstructionSet AVX2                 -> /arch:AVX2
#   LanguageStandard     stdcpp20                     -> /std:c++20
#   WarningLevel         Level3                       -> /W3
#   MultiProcessorCompilation                         -> /MP
#   AdditionalOptions    /Zc:__cplusplus /permissive-
#   WholeProgramOptimization false                    -> (no /GL)
#
# The project's file list is exactly src/pcl/*.cpp -- all 192 sources in
# 2.10.8, with no exclusions and no per-file overrides -- so the glob below
# reproduces it for any PCL version.
#
# Invoked by scripts/build_pcl.ps1; not part of Repatch's own build.
# ---------------------------------------------------------------------------

if(NOT MSVC)
  message(FATAL_ERROR "This project builds PCL with MSVC on Windows only.")
endif()
if(NOT PCL_DIR OR NOT EXISTS "${PCL_DIR}/include/pcl/Version.h")
  message(FATAL_ERROR "PCL_DIR='${PCL_DIR}' is not a PCL checkout. Pass -DPCL_DIR=<path>.")
endif()
if(NOT PCL_LIB_OUT)
  message(FATAL_ERROR "Pass -DPCL_LIB_OUT=<dir> for the output directory.")
endif()

# PCL's own builds define neither NDEBUG (Windows uses the differently spelled
# _NDEBUG; macOS defines only _REENTRANT and __PCL_MACOSX), so assert() is
# live in an official PCL. CMake's Release flags add /DNDEBUG, which would
# silently disable it -- strip it to match. /EHsc likewise has to go, because
# PCL is built with the asynchronous model.
string(REGEX REPLACE "[-/]DNDEBUG *" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REGEX REPLACE "/EHsc *" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

file(GLOB PCL_SOURCES "${PCL_DIR}/src/pcl/*.cpp")
list(LENGTH PCL_SOURCES PCL_SOURCE_COUNT)
if(PCL_SOURCE_COUNT EQUAL 0)
  message(FATAL_ERROR "No sources found in ${PCL_DIR}/src/pcl/")
endif()
message(STATUS "PCL sources: ${PCL_SOURCE_COUNT}")

add_library(PCL-pxi STATIC ${PCL_SOURCES})

set_target_properties(PCL-pxi PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
  # A generator expression suppresses the per-configuration subdirectory the
  # Visual Studio generator would otherwise append, so the .lib lands exactly
  # where PCL's own projects put it ($PCLLIBDIR64).
  ARCHIVE_OUTPUT_DIRECTORY "$<1:${PCL_LIB_OUT}>")

target_include_directories(PCL-pxi PRIVATE
  "${PCL_DIR}/include" "${PCL_DIR}/src/3rdparty")

target_compile_definitions(PCL-pxi PRIVATE
  WIN32 WIN64 _WINDOWS UNICODE _UNICODE
  __PCL_WINDOWS __PCL_NO_WIN32_MINIMUM_VERSIONS __PCL_AVX2 __PCL_FMA _NDEBUG)

target_compile_options(PCL-pxi PRIVATE
  /O2 /Ob2 /Oi /Ot /Oy /GF /EHa /GS- /GR /fp:fast /arch:AVX2
  /Zc:__cplusplus /permissive- /MP /W3
  # PCL's headers are warning-clean at /W3 for its own build; these are the
  # categories MSVC raises in PCL's template-heavy code and PCL ignores.
  /wd4267 /wd4244 /wd4305)
