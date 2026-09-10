cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/generated_file.cmake")
if(NOT DEFINED DEV_CONFIG OR NOT DEFINED RESULT)
    message(FATAL_ERROR "DEV_CONFIG and RESULT are required")
endif()
# A child script gives every failed configure, build, read or copy one result.
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${DEV_CONFIG}" "-DRESULT=${RESULT}"
    "-DSKIP_BUILD=${SKIP_BUILD}" "-DJOBS=${JOBS}" "-DFORCE_REBUILD=${FORCE_REBUILD}" -P "${CMAKE_CURRENT_LIST_DIR}/dev_build_worker.cmake"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    chibi_write_generated("${RESULT}" "{\"status\":\"failed\"}\n")
endif()
