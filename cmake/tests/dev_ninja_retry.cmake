cmake_minimum_required(VERSION 3.20)
include("${ROOT}/cmake/generated_file.cmake")
file(REMOVE_RECURSE "${TEST_DIR}/.chibi/generations")
file(REMOVE "${TEST_DIR}/.chibi/application.current")
file(MAKE_DIRECTORY "${TEST_DIR}/src")
file(WRITE "${TEST_DIR}/src/host.c" "int main(void) { return 0; }\n")
file(WRITE "${TEST_DIR}/src/application.c" "int value(void) { return 1; }\n")
file(WRITE "${TEST_DIR}/src/resident.glsl" "resident version one\n")
file(WRITE "${TEST_DIR}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)\nproject(retry C)\nadd_executable(chibi src/host.c)\nadd_library(chibi_dll SHARED src/application.c)\n")
set(toolchain)
if(TEST_C_COMPILER)
    list(APPEND toolchain "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}")
endif()
if(TEST_OSX_SYSROOT)
    list(APPEND toolchain "-DCMAKE_OSX_SYSROOT=${TEST_OSX_SYSROOT}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -G Ninja -S "${TEST_DIR}" -B "${TEST_DIR}/build" ${toolchain}
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_DIR}/build" COMMAND_ERROR_IS_FATAL ANY)
if(APPLE)
    set(extension dylib)
else()
    set(extension so)
endif()
set(config "{\"version\":1,\"libraries\":[],\"suffixes\":[\".c\",\".glsl\"]}")
set(source_dir "${TEST_DIR}")
set(build_dir "${TEST_DIR}/build")
set(cmake "${CMAKE_COMMAND}")
set(host "${build_dir}/chibi")
set(application "${build_dir}/libchibi_dll.${extension}")
foreach(key source_dir build_dir cmake host application)
    chibi_json_string(value "${${key}}")
    string(JSON config SET "${config}" "${key}" "${value}")
endforeach()
chibi_json_string(value "${TEST_DIR}/src")
string(JSON config SET "${config}" roots "[${value}]")
file(WRITE "${TEST_DIR}/dev.json" "${config}")
file(TIMESTAMP "${host}" before "%s.%f")
file(SHA256 "${application}" application_before)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${TEST_DIR}/src/application.c" "int value(void) { return 2; }\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
    "-DRESULT=${TEST_DIR}/generation.json" -DFORCE_REBUILD=ON -DJOBS=2
    -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(TIMESTAMP "${host}" after "%s.%f")
file(SHA256 "${application}" application_after)
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON status GET "${generation}" status)
if(NOT before STREQUAL after OR application_before STREQUAL application_after OR NOT status STREQUAL "ready")
    message(FATAL_ERROR "Retry must rebuild application, preserve resident host, and stage a ready generation")
endif()
string(JSON protected GET "${generation}" directory)
file(LOCK "${protected}/.lease" GUARD PROCESS TIMEOUT 0)

# A matching build fingerprint skips configure/build, while a matching current
# generation skips staging and keeps its immutable application path.
file(TIMESTAMP "${host}" cached_host_before "%s.%f")
file(TIMESTAMP "${application}" cached_application_before "%s.%f")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
    "-DRESULT=${TEST_DIR}/generation.json"
    -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(TIMESTAMP "${host}" cached_host_after "%s.%f")
file(TIMESTAMP "${application}" cached_application_after "%s.%f")
if(NOT cached_host_before STREQUAL cached_host_after OR
   NOT cached_application_before STREQUAL cached_application_after)
    message(FATAL_ERROR "An unchanged build fingerprint must leave build outputs untouched")
endif()
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON current GET "${generation}" directory)
string(JSON current_application GET "${generation}" application)
string(JSON current_resident GET "${generation}" resident)
file(WRITE "${TEST_DIR}/.chibi/application.current" "${current_application}\n")
file(GLOB generations_before LIST_DIRECTORIES true "${TEST_DIR}/.chibi/generations/*")
list(LENGTH generations_before count_before)
foreach(attempt RANGE 1 10)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
        "-DRESULT=${TEST_DIR}/generation.json" -DSKIP_BUILD=ON
        -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
file(GLOB generations LIST_DIRECTORIES true "${TEST_DIR}/.chibi/generations/*")
list(LENGTH generations count)
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON latest GET "${generation}" directory)
string(JSON latest_application GET "${generation}" application)
if(NOT count EQUAL count_before OR NOT latest STREQUAL current OR
   NOT latest_application STREQUAL current_application OR NOT EXISTS "${protected}" OR
   NOT EXISTS "${current}" OR NOT EXISTS "${latest}")
    message(FATAL_ERROR "Unchanged input did not reuse the protected generation")
endif()

# A matching marker outside the generation tree is not eligible for reuse.
set(outside "${TEST_DIR}/outside")
file(MAKE_DIRECTORY "${outside}")
get_filename_component(current_name "${current_application}" NAME)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${current_application}" "${outside}/${current_name}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${current}/.chibi-generation" "${outside}/.chibi-generation"
    COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${TEST_DIR}/.chibi/application.current" "${outside}/${current_name}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
    "-DRESULT=${TEST_DIR}/generation.json" -DSKIP_BUILD=ON
    -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON confined_directory GET "${generation}" directory)
string(FIND "${confined_directory}/" "${TEST_DIR}/.chibi/generations/" in_generations)
if(NOT in_generations EQUAL 0)
    message(FATAL_ERROR "A manifest path outside the generation tree must not be reused")
endif()
file(WRITE "${TEST_DIR}/.chibi/application.current" "${current_application}\n")

# Same-source binary changes invalidate reuse.
file(APPEND "${application}" "same source, new binary bytes\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
    "-DRESULT=${TEST_DIR}/generation.json" -DSKIP_BUILD=ON
    -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON binary_directory GET "${generation}" directory)
if(binary_directory STREQUAL current)
    message(FATAL_ERROR "A changed application binary must invalidate generation reuse")
endif()

# Changed source and resident fingerprints both stage new generations.
file(SHA256 "${application}" app_before_change)
file(WRITE "${TEST_DIR}/src/application.c" "int value(void) { return 3; }\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
    "-DRESULT=${TEST_DIR}/generation.json" -DJOBS=2
    -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${application}" app_after_change)
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON changed_directory GET "${generation}" directory)
if(app_before_change STREQUAL app_after_change OR changed_directory STREQUAL binary_directory)
    message(FATAL_ERROR "Changed application sources must rebuild and stage a new generation")
endif()
file(WRITE "${TEST_DIR}/.chibi/application.current" "${current_application}\n")
file(WRITE "${TEST_DIR}/src/resident.glsl" "resident version two\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEV_CONFIG=${TEST_DIR}/dev.json"
    "-DRESULT=${TEST_DIR}/generation.json" -DJOBS=2
    -P "${ROOT}/cmake/dev_build_worker.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${TEST_DIR}/generation.json" generation)
string(JSON resident_directory GET "${generation}" directory)
string(JSON resident_after_change GET "${generation}" resident)
if(resident_directory STREQUAL changed_directory OR current_resident STREQUAL resident_after_change)
    message(FATAL_ERROR "Changed resident inputs must stage a generation with a new resident fingerprint")
endif()
message(STATUS "Ninja retry PASS: unchanged build and generation reused; changed application and resident inputs staged")
