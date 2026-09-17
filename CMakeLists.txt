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
set(REPATCH_PCL_ARCH "arm64" CACHE STRING "PCL library architecture subdirectory (lib/macosx/<arch>)")

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library: pure C++20, no PCL, no -ffast-math (determinism tests rely on it)
# ---------------------------------------------------------------------------
file(GLOB REPATCH_CORE_SOURCES CONFIGURE_DEPENDS src/core/repatch/*.cpp)
add_library(repatch_core STATIC ${REPATCH_CORE_SOURCES})
target_include_directories(repatch_core PUBLIC src/core)
target_compile_options(repatch_core PRIVATE -Wall -Wextra)
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

foreach(group mask parallel random pyramid stretch diffusion patchmatch backend fits stroke)
  add_test(NAME ${group} COMMAND repatch_tests ${group}_)
endforeach()
add_test(NAME no_pcl_deps
         COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/src/core -P ${CMAKE_SOURCE_DIR}/tests/check_no_pcl.cmake)

# ---------------------------------------------------------------------------
# PixInsight module (macOS arm64). Flags mirror PCL's generated makefiles
# (src/modules/processes/CloneStamp/macosx/g++/makefile-arm64).
# ---------------------------------------------------------------------------
if(REPATCH_BUILD_MODULE)
  if(NOT PCL_DIR OR NOT EXISTS "${PCL_DIR}/include/pcl/Version.h")
    message(FATAL_ERROR "REPATCH_BUILD_MODULE is ON but PCL_DIR='${PCL_DIR}' is not a PCL checkout. Pass -DPCL_DIR=/path/to/PCL or -DREPATCH_BUILD_MODULE=OFF.")
  endif()
  if(NOT APPLE)
    message(FATAL_ERROR "Phase 1 builds the module on macOS only.")
  endif()
  set(PCL_LIB_DIR "${PCL_DIR}/lib/macosx/${REPATCH_PCL_ARCH}")
  foreach(lib PCL lz4 zstd zlib RFC6234 lcms cminpack)
    if(NOT EXISTS "${PCL_LIB_DIR}/lib${lib}-pxi.a")
      message(FATAL_ERROR "Missing ${PCL_LIB_DIR}/lib${lib}-pxi.a - run scripts/build_pcl.sh first.")
    endif()
  endforeach()

  file(READ "${CMAKE_SOURCE_DIR}/rsc/icons/Repatch.svg" REPATCH_ICON_SVG)
  configure_file("${CMAKE_SOURCE_DIR}/src/module/RepatchIcon.h.in"
                 "${CMAKE_BINARY_DIR}/generated/RepatchIcon.h" @ONLY)

  file(GLOB REPATCH_MODULE_SOURCES CONFIGURE_DEPENDS src/module/*.cpp)
  add_library(Repatch-pxm SHARED ${REPATCH_MODULE_SOURCES} ${REPATCH_CORE_SOURCES})
  set_target_properties(Repatch-pxm PROPERTIES
    PREFIX "" OUTPUT_NAME "Repatch-pxm" SUFFIX ".dylib"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    OSX_ARCHITECTURES "${REPATCH_PCL_ARCH}")
  target_include_directories(Repatch-pxm PRIVATE
    src/core src/module "${CMAKE_BINARY_DIR}/generated" "${PCL_DIR}/include" "${PCL_DIR}/src/3rdparty")
  target_compile_definitions(Repatch-pxm PRIVATE _REENTRANT __PCL_MACOSX)
  target_compile_options(Repatch-pxm PRIVATE
    -pipe -pthread -fPIC -O3 -ffunction-sections -fdata-sections -ffast-math -stdlib=libc++
    -Wall -Wno-parentheses -Wno-missing-braces -Wno-nan-infinity-disabled -Wno-extern-c-compat)
  target_link_directories(Repatch-pxm PRIVATE "${PCL_LIB_DIR}")
  target_link_options(Repatch-pxm PRIVATE
    -stdlib=libc++ -Wl,-dead_strip -rdynamic -headerpad_max_install_names
    "-Wl,-install_name,@executable_path/Repatch-pxm.dylib")
  target_link_libraries(Repatch-pxm PRIVATE
    "-framework AppKit" pthread PCL-pxi lz4-pxi zstd-pxi zlib-pxi RFC6234-pxi lcms-pxi cminpack-pxi)

  set(REPATCH_BIN_DIR "${CMAKE_SOURCE_DIR}/bin/macosx/${REPATCH_PCL_ARCH}")
  add_custom_command(TARGET Repatch-pxm POST_BUILD
    COMMAND codesign -s - -f "$<TARGET_FILE:Repatch-pxm>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${REPATCH_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Repatch-pxm>" "${REPATCH_BIN_DIR}/Repatch-pxm.dylib"
    COMMENT "Ad-hoc signing Repatch-pxm.dylib and copying it to bin/")
endif()
