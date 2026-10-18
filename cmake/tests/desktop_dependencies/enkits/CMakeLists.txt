cmake_minimum_required(VERSION 3.24)
project(enkits_acquisition_fixture LANGUAGES NONE)

# Exercise the production decision without retrieving or configuring enkiTS.
function(FetchContent_Declare name)
    if(NOT "${name}" STREQUAL "enkits" OR NOT "${ARGN}" STREQUAL
       "GIT_REPOSITORY;https://github.com/dougbinks/enkiTS.git;GIT_TAG;4cba61a045d8e759bd35e5d85691bf9f7271f822;UPDATE_DISCONNECTED;TRUE")
        message(FATAL_ERROR "fixture: changed enkiTS acquisition pin/options")
    endif()
    get_property(count GLOBAL PROPERTY fixture_declarations)
    math(EXPR count "${count} + 1")
    set_property(GLOBAL PROPERTY fixture_declarations "${count}")
endfunction()

function(FetchContent_MakeAvailable name)
    if(NOT ARGC EQUAL 1 OR NOT "${name}" STREQUAL "enkits")
        message(FATAL_ERROR "fixture: unexpected dependency acquisition")
    endif()
    if(NOT ENKITS_BUILD_C_INTERFACE OR ENKITS_BUILD_EXAMPLES OR
       ENKITS_BUILD_SHARED OR ENKITS_INSTALL OR ENKITS_SANITIZE)
        message(FATAL_ERROR "fixture: changed enkiTS configuration")
    endif()
    get_property(count GLOBAL PROPERTY fixture_acquisitions)
    math(EXPR count "${count} + 1")
    set_property(GLOBAL PROPERTY fixture_acquisitions "${count}")
endfunction()

set_property(GLOBAL PROPERTY fixture_declarations 0)
set_property(GLOBAL PROPERTY fixture_acquisitions 0)
include("${CHIBI_ROOT}/cmake/enkits.cmake")
get_property(declarations GLOBAL PROPERTY fixture_declarations)
get_property(acquisitions GLOBAL PROPERTY fixture_acquisitions)
if(NOT declarations EQUAL 1 OR NOT acquisitions EQUAL 1)
    message(FATAL_ERROR "fixture: standalone enkiTS acquisition was omitted")
endif()

add_library(chibi_task_scheduler STATIC IMPORTED)
set_target_properties(chibi_task_scheduler PROPERTIES
    IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/sdk-owned.a"
    INTERFACE_COMPILE_DEFINITIONS "FIXTURE_SDK_SCHEDULER=1")
include("${CHIBI_ROOT}/cmake/enkits.cmake")
get_property(declarations GLOBAL PROPERTY fixture_declarations)
get_property(acquisitions GLOBAL PROPERTY fixture_acquisitions)
if(NOT declarations EQUAL 1 OR NOT acquisitions EQUAL 1)
    message(FATAL_ERROR "fixture: SDK scheduler reacquired enkiTS")
endif()
get_target_property(location chibi_task_scheduler IMPORTED_LOCATION)
get_target_property(definitions chibi_task_scheduler INTERFACE_COMPILE_DEFINITIONS)
if(NOT location STREQUAL "${CMAKE_BINARY_DIR}/sdk-owned.a" OR
   NOT definitions STREQUAL "FIXTURE_SDK_SCHEDULER=1" OR TARGET enkiTS)
    message(FATAL_ERROR "fixture: SDK scheduler provider changed")
endif()
