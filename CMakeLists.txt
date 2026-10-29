cmake_minimum_required(VERSION 3.20)
project(chibi LANGUAGES C CXX)
# Windows executables embed their manifest, and link the C runtime
# statically so exported games run without the Visual C++ redistributable.
# Dependencies always build in Release, so every configuration links the
# release runtime and shares one C runtime with them.
if(WIN32)
    enable_language(RC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # The C++ shims and dependencies use no modules or dependency scanner.
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    # Recursive graph publication exceeds Emscripten's 64 KiB default with a
    # supported 64-operation data chain. Keep it within an explicit stack budget.
    add_link_options(-sSTACK_SIZE=1048576)
endif()

if(CMAKE_HOST_APPLE AND CMAKE_OSX_SYSROOT)
    set(CHIBI_SDKROOT "${CMAKE_OSX_SYSROOT}")
elseif(CMAKE_HOST_APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CHIBI_SDKROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHIBI_FIELD_SIMD "Enable compiler SIMD in worker Web field evaluation" ON)

option(CHIBI_ENABLE_SANITIZERS
       "Enable the selected sanitizer for native targets" ON)
option(CHIBI_ENABLE_CCACHE "Use ccache when it is available" ON)
option(CHIBI_DISTRIBUTION "Load native resources only from the installed package" OFF)
if(CHIBI_DISTRIBUTION AND CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR "CHIBI_DISTRIBUTION requires the static native application")
endif()
if(CHIBI_DISTRIBUTION)
    set_source_files_properties(src/support/resources.c PROPERTIES
        COMPILE_DEFINITIONS CHIBI_DISTRIBUTION=1)
else()
    set_source_files_properties(src/support/resources.c PROPERTIES
        COMPILE_DEFINITIONS "CHIBI_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
endif()
if(CHIBI_ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${CMAKE_BINARY_DIR}" NORMALIZE CHIBI_IN_TREE_BUILD)
        if(NOT CMAKE_CONFIGURATION_TYPES AND CHIBI_IN_TREE_BUILD AND
           (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CMAKE_BUILD_TYPE STREQUAL "Release"))
            cmake_path(RELATIVE_PATH CMAKE_BINARY_DIR BASE_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE CHIBI_CCACHE_RELATIVE_BINARY_DIR)
            # Debug info only needs the directory depth to resolve ../ source paths. Naming every
            # component "build" lets differently named build directories share ccache objects.
            string(REGEX REPLACE "[^/]+" "build" CHIBI_CCACHE_DEBUG_BINARY_DIR
                "${CHIBI_CCACHE_RELATIVE_BINARY_DIR}")
        endif()
        foreach(language C CXX OBJC)
            if(NOT DEFINED CMAKE_${language}_COMPILER_LAUNCHER)
                set(CMAKE_${language}_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING
                    "${language} compiler launcher")
            endif()
            if(NOT CMAKE_CONFIGURATION_TYPES AND CHIBI_IN_TREE_BUILD AND
               (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CMAKE_BUILD_TYPE STREQUAL "Release") AND
               CMAKE_${language}_COMPILER_LAUNCHER STREQUAL CCACHE_PROGRAM)
                # Normalize in-tree paths so matching builds share objects across worktrees.
                set(CMAKE_${language}_COMPILER_LAUNCHER
                    "${CMAKE_COMMAND};-E;env;CCACHE_BASEDIR=${CMAKE_SOURCE_DIR};${CCACHE_PROGRAM}")
                if(CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
                    if(CMAKE_${language}_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
                        # Keep DWARF paths relative to the worktree root for debugger lookup.
                        string(APPEND CMAKE_${language}_FLAGS
                            " -fdebug-compilation-dir=${CHIBI_CCACHE_DEBUG_BINARY_DIR}")
                    elseif(CMAKE_${language}_COMPILER_ID STREQUAL "GNU")
                        string(APPEND CMAKE_${language}_FLAGS
                            " -fdebug-prefix-map=${CMAKE_BINARY_DIR}=${CHIBI_CCACHE_DEBUG_BINARY_DIR}")
                    endif()
                endif()
            endif()
        endforeach()
    endif()
endif()

# libclang (build tool only; runtime stays dependency-free).
set(LIBCLANG_ROOT "" CACHE PATH "LLVM installation prefix")
# Debian and Ubuntu install LLVM under /usr/lib/llvm-<major>; prefer the newest.
file(GLOB LIBCLANG_DISTRO_ROOTS /usr/lib/llvm-*)
list(SORT LIBCLANG_DISTRO_ROOTS COMPARE NATURAL ORDER DESCENDING)
set(LIBCLANG_HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm ${LIBCLANG_DISTRO_ROOTS})
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS ${LIBCLANG_HINTS}
    PATH_SUFFIXES include)
find_library(LIBCLANG_LIBRARY NAMES clang libclang clang-18 libclang-18
    HINTS ${LIBCLANG_HINTS}
    PATH_SUFFIXES lib)
list(TRANSFORM LIBCLANG_HINTS APPEND "/lib/clang/*/include" OUTPUT_VARIABLE LIBCLANG_RESOURCE_GLOBS)
file(GLOB LIBCLANG_RESOURCE_INCLUDE_DIRS ${LIBCLANG_RESOURCE_GLOBS})
list(LENGTH LIBCLANG_RESOURCE_INCLUDE_DIRS LIBCLANG_RESOURCE_COUNT)
if(LIBCLANG_RESOURCE_COUNT)
    list(GET LIBCLANG_RESOURCE_INCLUDE_DIRS 0 LIBCLANG_RESOURCE_INCLUDE_DIR)
endif()
if(NOT CMAKE_CROSSCOMPILING AND (NOT LIBCLANG_INCLUDE_DIR OR NOT LIBCLANG_LIBRARY))
  message(FATAL_ERROR "libclang not found; set LIBCLANG_ROOT to an LLVM installation")
endif()

option(CHIBI_ENABLE_WARNINGS
       "Enable strict compiler warnings for project targets" ON)
set(CHIBI_SANITIZERS "address,undefined" CACHE STRING
    "Sanitizer runtime to use: address, undefined, address,undefined, or thread")
set_property(CACHE CHIBI_SANITIZERS PROPERTY STRINGS
    address undefined "address,undefined" thread)

if(CHIBI_ENABLE_WARNINGS AND
   (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
    CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-zero-length-array
        -Wno-string-concatenation
    )
endif()

if(CHIBI_ENABLE_SANITIZERS AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND
   (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
    CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    set(CHIBI_SUPPORTED_SANITIZERS address undefined "address,undefined" thread)
    if(NOT CHIBI_SANITIZERS IN_LIST CHIBI_SUPPORTED_SANITIZERS)
        message(FATAL_ERROR
            "CHIBI_SANITIZERS must be address, undefined, address,undefined, or thread")
    endif()
    add_compile_options("-fsanitize=${CHIBI_SANITIZERS}" -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${CHIBI_SANITIZERS}")
endif()

# The web profiling build names WASM functions for the browser's profiler and
# shows trace zones as User Timing measures (make web-profile).
option(CHIBI_WEB_PROFILING "Name functions and trace zones in the web build" OFF)
if(CHIBI_WEB_PROFILING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(FATAL_ERROR "CHIBI_WEB_PROFILING is only supported by web builds")
    endif()
    add_compile_definitions(CHIBI_ENABLE_WEB_TRACE=1)
    add_link_options(--profiling-funcs)
endif()

# Emscripten labels its SDL3 port experimental independently of source warnings.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_compile_options(-Wno-experimental)
    add_link_options(-Wno-experimental)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)
endif()

# Root products compose the same scoped packages used by installed consumers.
# Studio is built in its own repository against the installed SDK.
include(cmake/sources.cmake)
option(CHIBI_SDK_BUILD_COMPILER "Build compiler and authoring packages" ON)
option(CHIBI_SDK_BUILD_NATIVE "Build native component/capability packages" ON)
option(CHIBI_SDK_BUILD_DESKTOP "Build desktop player and controller packages" ON)
option(CHIBI_BUILD_TESTS "Build the admitted package-consumer C test drivers" ON)

set(CHIBI_REFLECT_EXECUTABLE "" CACHE FILEPATH "Native reflection generator for cross builds")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(CHIBI_SDK_BUILD_NATIVE)
    set(CHIBI_NATIVE_TYPES_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/native_types.c")
    set(CHIBI_NATIVE_BINDINGS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/native_capabilities.c")
endif()
add_subdirectory(sdk)

# Only reflection consumes libclang; runtime and compiler artifacts do not.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(reflect ${CHIBI_REFLECT_SOURCES})
    target_include_directories(reflect PRIVATE "${LIBCLANG_INCLUDE_DIR}")
    include("${CMAKE_CURRENT_SOURCE_DIR}/tools/reflect/resource.cmake")
    chibi_reflect_resource_include(LIBCLANG_RESOURCE_INCLUDE_DIR)
    target_compile_definitions(reflect PRIVATE
        CHIBI_REFLECT_RESOURCE_INCLUDE_DIR="${LIBCLANG_RESOURCE_INCLUDE_DIR}")
    target_link_libraries(reflect PRIVATE "${LIBCLANG_LIBRARY}" Chibi::capabilities Chibi::execution_contract)
    get_filename_component(libclang_dir "${LIBCLANG_LIBRARY}" DIRECTORY)
    set_target_properties(reflect PROPERTIES BUILD_RPATH "${libclang_dir}")
    set(reflect_command "$<TARGET_FILE:reflect>")
    set(reflect_dependency reflect)
elseif(CHIBI_SDK_BUILD_NATIVE)
    if(NOT EXISTS "${CHIBI_REFLECT_EXECUTABLE}")
        message(FATAL_ERROR "Cross builds require a native CHIBI_REFLECT_EXECUTABLE")
    endif()
    set(reflect_command "${CHIBI_REFLECT_EXECUTABLE}")
    set(reflect_dependency "${CHIBI_REFLECT_EXECUTABLE}")
endif()
if(CHIBI_SDK_BUILD_NATIVE)
    set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/component_ids.tsv")
    set(reflect_args --include "${CMAKE_CURRENT_SOURCE_DIR}/src"
        --include "${CMAKE_CURRENT_BINARY_DIR}/sdk/public/math" --manifest "${manifest}")
    if(CHIBI_SDKROOT)
        list(APPEND reflect_args --sysroot "${CHIBI_SDKROOT}")
    endif()
    add_custom_command(OUTPUT "${CHIBI_NATIVE_TYPES_SOURCE}.stamp"
        BYPRODUCTS "${CHIBI_NATIVE_TYPES_SOURCE}" "${CHIBI_NATIVE_BINDINGS_SOURCE}"
        COMMAND ${reflect_command} "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/native_nodes.c"
            --prefix "" --types-output "${CHIBI_NATIVE_TYPES_SOURCE}"
            --runtime-output "${CHIBI_NATIVE_BINDINGS_SOURCE}" ${reflect_args}
        COMMAND ${CMAKE_COMMAND} -E touch "${CHIBI_NATIVE_TYPES_SOURCE}.stamp"
        DEPENDS ${reflect_dependency} "${manifest}" src/runtime/native_nodes.c
            src/runtime/interface.h src/runtime/components.h src/types/annotate.h
        VERBATIM)
    add_custom_target(generate_runtime_registry DEPENDS "${CHIBI_NATIVE_TYPES_SOURCE}.stamp")
    add_dependencies(chibi_native_types generate_runtime_registry)
    add_dependencies(chibi_native_bindings generate_runtime_registry)
endif()

if(TARGET chibi)
    # Build-tree players read shaders, chibi version the build identity, chibi
    # test the core collection and chibi doc the site generator from
    # ../share/chibi, as installed ones do.
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/sdk/chibi_engine_metadata.json"
        "${CMAKE_CURRENT_BINARY_DIR}/share/chibi/engine.json" COPYONLY)
    file(GLOB_RECURSE core_sources CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/packages/core" "${CMAKE_CURRENT_SOURCE_DIR}/packages/core/*.chibi")
    foreach(source IN LISTS core_sources)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packages/core/${source}"
            "${CMAKE_CURRENT_BINARY_DIR}/share/chibi/core/${source}" COPYONLY)
    endforeach()
    foreach(source markdown/Markdown.chibi site/Site.chibi reference/Reference.chibi grammar/Grammar.chibi)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/${source}"
            "${CMAKE_CURRENT_BINARY_DIR}/share/chibi/docs/${source}" COPYONLY)
    endforeach()
    foreach(source compiler_syntax/Syntax.chibi syntax_diagnostics/Diagnostics.chibi)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tools/compiler/${source}"
            "${CMAKE_CURRENT_BINARY_DIR}/share/chibi/docs/${source}" COPYONLY)
    endforeach()
    add_custom_target(chibi_build_shaders
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
            "${CMAKE_CURRENT_BINARY_DIR}/share/chibi/shaders")
    add_dependencies(chibi chibi_build_shaders)
endif()

# The C tests use POSIX processes and files directly.
if(CHIBI_BUILD_TESTS AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if(TARGET Chibi::authoring)
        add_subdirectory(test/language)
    endif()
    add_executable(test_host test/host/main.c test/host/export.c test/host/process.c
        test/host/state_file.c src/host/export.c src/host/process.c src/host/state_file.c
        src/support/xalloc.c third_party/yyjson/yyjson.c)
    set(host_test_headers "${CMAKE_CURRENT_BINARY_DIR}/testing/host/include")
    foreach(header export process state_file)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/host/${header}.h"
            "${host_test_headers}/host/${header}.h" COPYONLY)
    endforeach()
    foreach(header array xalloc)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/support/${header}.h"
            "${host_test_headers}/support/${header}.h" COPYONLY)
    endforeach()
    target_include_directories(test_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/third_party/pico_headers/pico_unit.h"
        "${host_test_headers}/third_party/pico_headers/pico_unit.h" COPYONLY)
    target_include_directories(test_host PRIVATE "${host_test_headers}")
    target_compile_options(test_host PRIVATE -UNDEBUG)
    add_executable(concurrent_runner test/host/concurrent.c src/host/process.c)
    target_include_directories(concurrent_runner PRIVATE "${host_test_headers}")
    target_compile_options(concurrent_runner PRIVATE -UNDEBUG)
    find_program(CHIBI_PYTHON_EXECUTABLE python3 REQUIRED)
    add_custom_target(check-host
        COMMAND "${CHIBI_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/test_policy.py" disabled check-host
        VERBATIM)
endif()

# Windows needs Developer Mode for symbolic links, so editors there read the
# build tree's compile commands directly.
if(NOT CMAKE_HOST_WIN32)
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC
        RESULT CHIBI_COMPILE_COMMANDS_LINK_RESULT)
    if(NOT CHIBI_COMPILE_COMMANDS_LINK_RESULT STREQUAL "0" AND
       (NOT IS_SYMLINK "${CMAKE_SOURCE_DIR}/compile_commands.json" OR
        NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json"))
        message(FATAL_ERROR "Could not publish compile commands: ${CHIBI_COMPILE_COMMANDS_LINK_RESULT}")
    endif()
endif()
