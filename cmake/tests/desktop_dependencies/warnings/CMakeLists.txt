cmake_minimum_required(VERSION 3.24)
project(desktop_dependency_warnings_fixture LANGUAGES NONE)

# Configure-only targets inspect the production boundary without detecting a
# compiler, compiling a source, or fetching/configuring vendor packages.
include("${CHIBI_ROOT}/sdk/dependency_options.cmake")
set(project_options -Wall -fsanitize=address,undefined -Werror
    -fno-omit-frame-pointer -Wextra -pthread -Wno-unused-parameter
    -DFIXTURE_OPTION=-Wpreserved)
set(dependency_options -fsanitize=address,undefined -fno-omit-frame-pointer
    -pthread -DFIXTURE_OPTION=-Wpreserved)
add_compile_options(${project_options})

chibi_dependency_options_begin()
set(fixture_target vendor)
set(fixture_expected "${dependency_options}")
add_subdirectory(target vendor)
chibi_dependency_options_end()

get_directory_property(restored COMPILE_OPTIONS)
if(NOT "${restored}" STREQUAL "${project_options}")
    message(FATAL_ERROR "fixture: Chibi directory options were not restored exactly")
endif()
if(NOT "${fixture_dependency_output}" STREQUAL "vendor")
    message(FATAL_ERROR "fixture: dependency-provided variables escaped the caller's scope")
endif()

set(fixture_target chibi)
set(fixture_expected "${project_options}")
add_subdirectory(target chibi)
