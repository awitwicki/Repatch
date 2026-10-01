cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14" CACHE STRING "Minimum macOS version")
project(Repatch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Default REPATCH_BUILD_MODULE to ON only when PCL_DIR is actually available
# (either passed explicitly via -DPCL_DIR=... or via the PCLDIR environment
# variable), so a bare `cmake -S . -B build` with no PCL info configures
# successfully with the module target skipped, rather than hard-failing.
# An explicit -DREPATCH_BUILD_MODULE=ON/OFF on the command line always wins.
# Note: the computed default is deliberately stored in a differently-named
# variable (not REPATCH_BUILD_MODULE itself) before calling option() below --
# under policy CMP0077 (NEW, the default here since cmake_minimum_required
# requests 3.20+), option() silently declines to create a cache entry when a
# normal variable of the same name already exists, which would leave
# REPATCH_BUILD_MODULE completely absent from the cache.
if(DEFINED REPATCH_BUILD_MODULE)
  set(REPATCH_BUILD_MODULE_DEFAULT ${REPATCH_BUILD_MODULE})
elseif(PCL_DIR OR NOT "$ENV{PCLDIR}" STREQUAL "")
  set(REPATCH_BUILD_MODULE_DEFAULT ON)
else()
  set(REPATCH_BUILD_MODULE_DEFAULT OFF)
endif()
option(REPATCH_BUILD_MODULE "Build the PixInsight module (requires PCL_DIR)" ${REPATCH_BUILD_MODULE_DEFAULT})
set(PCL_DIR "$ENV{PCLDIR}" CACHE PATH "PCL root directory (contains include/ and lib/)")

# PCL keeps its static libraries under lib/<platform>/<arch> and this project
# mirrors that layout for bin/. Both default to the host's supported target:
# macOS arm64 or Windows x64.
if(WIN32)
  set(REPATCH_PCL_PLATFORM_DEFAULT "windows")
  set(REPATCH_PCL_ARCH_DEFAULT "x64")
else()
  set(REPATCH_PCL_PLATFORM_DEFAULT "macosx")
  set(REPATCH_PCL_ARCH_DEFAULT "arm64")
endif()
set(REPATCH_PCL_PLATFORM "${REPATCH_PCL_PLATFORM_DEFAULT}" CACHE STRING "PCL platform subdirectory (lib/<platform>/<arch>)")
set(REPATCH_PCL_ARCH "${REPATCH_PCL_ARCH_DEFAULT}" CACHE STRING "PCL architecture subdirectory (lib/<platform>/<arch>)")

find_package(Threads REQUIRED)

# MSVC injects /EHsc into CMAKE_CXX_FLAGS for every target, but the module has
# to be built with PCL's asynchronous exception model (/EHa). Strip the global
# flag and set the model per target instead, so that the module does not
# provoke a D9025 "overriding /EHsc with /EHa" warning on every file.
if(MSVC)
  string(REGEX REPLACE "/EHsc *" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# Core library: pure C++20, no PCL, no fast-math (determinism tests rely on it)
# ---------------------------------------------------------------------------
file(GLOB REPATCH_CORE_SOURCES CONFIGURE_DEPENDS src/core/repatch/*.cpp)
add_library(repatch_core STATIC ${REPATCH_CORE_SOURCES})
target_include_directories(repatch_core PUBLIC src/core)
if(MSVC)
  target_compile_options(repatch_core PRIVATE /W4 /EHsc /fp:precise)
else()
  target_compile_options(repatch_core PRIVATE -Wall -Wextra)
endif()
target_link_libraries(repatch_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# CLI harness
# ---------------------------------------------------------------------------
file(GLOB REPATCH_CLI_SOURCES CONFIGURE_DEPENDS src/cli/*.cpp)
add_executable(repatch-cli ${REPATCH_CLI_SOURCES})
target_include_directories(repatch-cli PRIVATE src/cli)
target_link_libraries(repatch-cli PRIVATE repatch_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
file(GLOB REPATCH_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
list(FILTER REPATCH_CLI_SOURCES EXCLUDE REGEX "main\\.cpp$")
add_executable(repatch_tests ${REPATCH_TEST_SOURCES} ${REPATCH_CLI_SOURCES})
target_include_directories(repatch_tests PRIVATE tests src/cli)
target_link_libraries(repatch_tests PRIVATE repatch_core)

# The FITS reader/writer uses std::fopen, which MSVC deprecates (C4996).
if(MSVC)
  foreach(target repatch-cli repatch_tests)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE /EHsc /fp:precise)
  endforeach()
endif()

foreach(group mask parallel random pyramid stretch diffusion patchmatch backend fits stroke)
  add_test(NAME ${group} COMMAND repatch_tests ${group}_)
endforeach()
add_test(NAME no_pcl_deps
         COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/src/core -P ${CMAKE_SOURCE_DIR}/tests/check_no_pcl.cmake)

# ---------------------------------------------------------------------------
# PixInsight module. Compiler and linker flags mirror PCL's own generated
# build files for each platform, because the module is loaded into the
# PixInsight core process and has to match the ABI PCL was built with:
#   macOS arm64 - src/modules/processes/CloneStamp/macosx/g++/makefile-arm64
#   Windows x64 - src/pcl/windows/vc17/PCL.vcxproj
# ---------------------------------------------------------------------------
if(REPATCH_BUILD_MODULE)
  if(NOT PCL_DIR OR NOT EXISTS "${PCL_DIR}/include/pcl/Version.h")
    message(FATAL_ERROR "REPATCH_BUILD_MODULE is ON but PCL_DIR='${PCL_DIR}' is not a PCL checkout. Pass -DPCL_DIR=/path/to/PCL or -DREPATCH_BUILD_MODULE=OFF.")
  endif()
  if(NOT APPLE AND NOT (WIN32 AND MSVC))
    message(FATAL_ERROR "The module builds on macOS (clang) and Windows (MSVC) only.")
  endif()

  if(APPLE)
    set(PCL_LIB_DIR "${PCL_DIR}/lib/macosx/${REPATCH_PCL_ARCH}")
  else()
    set(PCL_LIB_DIR "${PCL_DIR}/lib/${REPATCH_PCL_PLATFORM}/${REPATCH_PCL_ARCH}")
  endif()
  foreach(lib PCL lz4 zstd zlib RFC6234 lcms cminpack)
    if(WIN32)
      set(REPATCH_LIB_FILE "${PCL_LIB_DIR}/${lib}-pxi.lib")
      set(REPATCH_LIB_HINT "run scripts/build_pcl.ps1 first")
    else()
      set(REPATCH_LIB_FILE "${PCL_LIB_DIR}/lib${lib}-pxi.a")
      set(REPATCH_LIB_HINT "run scripts/build_pcl.sh first")
    endif()
    if(NOT EXISTS "${REPATCH_LIB_FILE}")
      message(FATAL_ERROR "Missing ${REPATCH_LIB_FILE} - ${REPATCH_LIB_HINT}.")
    endif()
  endforeach()

  file(READ "${CMAKE_SOURCE_DIR}/rsc/icons/Repatch.svg" REPATCH_ICON_SVG)
  configure_file("${CMAKE_SOURCE_DIR}/src/module/RepatchIcon.h.in"
                 "${CMAKE_BINARY_DIR}/generated/RepatchIcon.h" @ONLY)

  file(GLOB REPATCH_MODULE_SOURCES CONFIGURE_DEPENDS src/module/*.cpp)
  add_library(Repatch-pxm SHARED ${REPATCH_MODULE_SOURCES} ${REPATCH_CORE_SOURCES})
  target_include_directories(Repatch-pxm PRIVATE
    src/core src/module "${CMAKE_BINARY_DIR}/generated" "${PCL_DIR}/include" "${PCL_DIR}/src/3rdparty")
  target_link_directories(Repatch-pxm PRIVATE "${PCL_LIB_DIR}")

  if(APPLE)
    set_target_properties(Repatch-pxm PROPERTIES
      PREFIX "" OUTPUT_NAME "Repatch-pxm" SUFFIX ".dylib"
      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
      OSX_ARCHITECTURES "${REPATCH_PCL_ARCH}")
    target_compile_definitions(Repatch-pxm PRIVATE _REENTRANT __PCL_MACOSX)
    target_compile_options(Repatch-pxm PRIVATE
      -pipe -pthread -fPIC -O3 -ffunction-sections -fdata-sections -ffast-math -stdlib=libc++
      -Wall -Wno-parentheses -Wno-missing-braces -Wno-nan-infinity-disabled -Wno-extern-c-compat)
    target_link_options(Repatch-pxm PRIVATE
      -stdlib=libc++ -Wl,-dead_strip -rdynamic -headerpad_max_install_names
      "-Wl,-install_name,@executable_path/Repatch-pxm.dylib")
    target_link_libraries(Repatch-pxm PRIVATE
      "-framework AppKit" pthread PCL-pxi lz4-pxi zstd-pxi zlib-pxi RFC6234-pxi lcms-pxi cminpack-pxi)
  else()
    set_target_properties(Repatch-pxm PROPERTIES
      PREFIX "" OUTPUT_NAME "Repatch-pxm" SUFFIX ".dll"
      # PCL is built against the DLL runtime in both of its configurations, so
      # the module uses it too, whichever configuration it is built in.
      MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_definitions(Repatch-pxm PRIVATE
      WIN32 WIN64 _WINDOWS UNICODE _UNICODE
      __PCL_WINDOWS __PCL_NO_WIN32_MINIMUM_VERSIONS __PCL_AVX2 __PCL_FMA)
    # /EHa, /fp:fast, /arch:AVX2 and /GS- come straight from PCL.vcxproj;
    # /Zc:__cplusplus and /permissive- keep PCL's headers conforming.
    target_compile_options(Repatch-pxm PRIVATE
      /EHa /fp:fast /arch:AVX2 /GS- /GR /Oi /Ot /Oy /GF /Gy
      /Zc:__cplusplus /permissive- /MP /W3)
    # /OPT:REF,ICF is the MSVC equivalent of the macOS -dead_strip.
    target_link_options(Repatch-pxm PRIVATE /OPT:REF /OPT:ICF)
    # dbghelp for PCL's Win32Exception stack traces (it also asks for the
    # library with a #pragma comment, but naming it here keeps the dependency
    # visible); userenv for GetUserProfileDirectoryW in pcl::File::HomeDirectory.
    # Everything else PCL needs is in CMake's default Windows library set.
    target_link_libraries(Repatch-pxm PRIVATE
      PCL-pxi lz4-pxi zstd-pxi zlib-pxi RFC6234-pxi lcms-pxi cminpack-pxi
      dbghelp userenv)
  endif()

  if(APPLE)
    set(REPATCH_BIN_DIR "${CMAKE_SOURCE_DIR}/bin/macosx/${REPATCH_PCL_ARCH}")
    # Ad-hoc signature: required by the dynamic loader on Apple Silicon. The
    # PixInsight .xsgn signature is a separate step (scripts/dev_sign_module.sh).
    add_custom_command(TARGET Repatch-pxm POST_BUILD
      COMMAND codesign -s - -f "$<TARGET_FILE:Repatch-pxm>"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${REPATCH_BIN_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Repatch-pxm>" "${REPATCH_BIN_DIR}/Repatch-pxm.dylib"
      COMMENT "Ad-hoc signing Repatch-pxm.dylib and copying it to bin/")
  else()
    set(REPATCH_BIN_DIR "${CMAKE_SOURCE_DIR}/bin/${REPATCH_PCL_PLATFORM}/${REPATCH_PCL_ARCH}")
    add_custom_command(TARGET Repatch-pxm POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${REPATCH_BIN_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Repatch-pxm>" "${REPATCH_BIN_DIR}/Repatch-pxm.dll"
      COMMENT "Copying Repatch-pxm.dll to bin/")
  endif()
endif()
