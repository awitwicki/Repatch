# Fails if any file under DIR includes a PCL header. Run as:
#   cmake -DDIR=<path to src/core> -P check_no_pcl.cmake
file(GLOB_RECURSE files "${DIR}/*.h" "${DIR}/*.cpp")
foreach(f ${files})
  file(STRINGS "${f}" hits REGEX "#include[ \t]*<pcl/")
  if(hits)
    message(FATAL_ERROR "PCL include found in core file: ${f}")
  endif()
endforeach()
message(STATUS "no PCL includes under ${DIR}")
