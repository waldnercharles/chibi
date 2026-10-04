cmake_minimum_required(VERSION 3.20)
project(chibi LANGUAGES C CXX)

option(CHIBI_WEB_THREADS "Build a separate web application with pthread workers" OFF)
set(CHIBI_WEB_WORKERS 4 CACHE STRING "Total worker budget for the threaded web build")
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # The C++ shims and dependencies use no modules or dependency scanner.
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    # Recursive graph publication exceeds Emscripten's 64 KiB default with a
    # supported 64-operation data chain. Keep it within an explicit stack budget.
    add_link_options(-sSTACK_SIZE=1048576)
endif()
if(CHIBI_WEB_THREADS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(FATAL_ERROR "CHIBI_WEB_THREADS requires Emscripten")
    endif()
    if(NOT CHIBI_WEB_WORKERS MATCHES "^[0-9]+$" OR
       CHIBI_WEB_WORKERS LESS 2 OR CHIBI_WEB_WORKERS GREATER 8)
        message(FATAL_ERROR "CHIBI_WEB_WORKERS must be between 2 and 8")
    endif()
    math(EXPR CHIBI_WEB_POOL_SIZE "${CHIBI_WEB_WORKERS} - 1")
    # The compatibility bundle remains scalar. Browsers rejecting the worker
    # bundle's shared memory or SIMD module fall back through its bootstrap.
    add_compile_options(-pthread -msimd128)
    add_compile_definitions(CHIBI_WEB_WORKERS=${CHIBI_WEB_WORKERS})
    add_link_options(-pthread -msimd128 "-sPTHREAD_POOL_SIZE=${CHIBI_WEB_POOL_SIZE}"
        -sPTHREAD_POOL_SIZE_STRICT=2 -sALLOW_MEMORY_GROWTH=1)
endif()

set(CHIBI_COLLAB_URL "" CACHE STRING "Public collaboration room service HTTP(S) URL")

option(CHIBI_ENABLE_PERFORMANCE_TESTS
       "Run timing-sensitive performance checks in the test suite" OFF)

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

# WASM executes separate multiply/add instructions. Preserve that rounding in
# fields, materials, particle seeding and physics on native builds too.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        src/graph/compiler/field_program.c
        src/runtime/material_destruction.c
        src/runtime/material_ecs.c
        src/runtime/material_mask.c
        src/runtime/material_ecs_fluid.c
        src/runtime/material_flow.c
        src/runtime/material_fluid.c
        src/runtime/material_mpm.c
        src/runtime/material_obstacle_index.c
        src/runtime/particle_emitters.c
        src/runtime/scenes/physics.c
        src/runtime/visual_transform.c
        src/render/particles.c
        PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()

option(CHIBI_FIELD_SIMD "Enable compiler SIMD in worker Web field evaluation" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND
   (NOT CHIBI_WEB_THREADS OR NOT CHIBI_FIELD_SIMD))
    set_property(SOURCE src/graph/compiler/field_program.c APPEND PROPERTY
        COMPILE_OPTIONS -mno-simd128)
endif()

option(CHIBI_ENABLE_SANITIZERS
       "Enable the selected sanitizer for native targets" ON)
option(CHIBI_ENABLE_CCACHE "Use ccache when it is available" ON)
option(CHIBI_DEVELOPMENT_RELOAD "Build the native reload host and application DLL" OFF)
option(CHIBI_DISTRIBUTION "Load native resources only from the installed package" OFF)
if(CHIBI_DISTRIBUTION AND (CHIBI_DEVELOPMENT_RELOAD OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
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
                            " -fdebug-compilation-dir=${CHIBI_CCACHE_RELATIVE_BINARY_DIR}")
                    elseif(CMAKE_${language}_COMPILER_ID STREQUAL "GNU")
                        string(APPEND CMAKE_${language}_FLAGS
                            " -fdebug-prefix-map=${CMAKE_BINARY_DIR}=${CHIBI_CCACHE_RELATIVE_BINARY_DIR}")
                    endif()
                endif()
            endif()
        endforeach()
    endif()
endif()

include(cmake/dependencies_import.cmake)
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
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND
   (NOT LIBCLANG_INCLUDE_DIR OR NOT LIBCLANG_LIBRARY))
  message(FATAL_ERROR "libclang not found; set LIBCLANG_ROOT to an LLVM installation")
endif()

option(CHIBI_ENABLE_WARNINGS
       "Enable strict compiler warnings for project targets" ON)
set(CHIBI_SANITIZERS "address,undefined" CACHE STRING
    "Sanitizer runtime to use: address, undefined, address,undefined, or thread")
set_property(CACHE CHIBI_SANITIZERS PROPERTY STRINGS
    address undefined "address,undefined" thread)

function(chibi_enable_tracy target)
    if(CHIBI_ENABLE_TRACY)
        target_compile_definitions(${target} PRIVATE CHIBI_ENABLE_TRACY=1)
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
    endif()
endfunction()

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
    if(TARGET enkiTS AND NOT CHIBI_DEPENDENCY_PACKAGE)
        target_compile_options(enkiTS PRIVATE "-fsanitize=${CHIBI_SANITIZERS}" -fno-omit-frame-pointer)
    endif()
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

# Project headers resolve from the source tree; only the reload ABI is public.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
include(cmake/sources.cmake)

# --- embedded editor icon font ---
function(chibi_embed input output symbol)
    add_custom_command(
        OUTPUT "${output}.stamp"
        BYPRODUCTS "${output}"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${input}" "-DOUTPUT=${output}" "-DSYMBOL=${symbol}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        COMMAND ${CMAKE_COMMAND} -E touch "${output}.stamp"
        DEPENDS "${input}" cmake/embed_binary.cmake cmake/generated_file.cmake
        VERBATIM)
    add_custom_target(generate_${symbol} DEPENDS "${output}.stamp")
endfunction()
set(GEN_NERD_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_nerd_symbols_font.c)
set(GEN_UI_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_ui_font.c)
set(GEN_UI_BOLD_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_ui_bold_font.c)
set(GEN_UI_MEDIUM_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_ui_medium_font.c)
set(GEN_UI_SEMIBOLD_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_ui_semibold_font.c)
set(GEN_UI_NUMERIC_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_ui_numeric_font.c)
set(GEN_NOTO_SANS_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_noto_sans_font.c)
set(GEN_NOTO_SANS_SEMIBOLD_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_noto_sans_semibold_font.c)
set(GEN_NOTO_SANS_BOLD_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_noto_sans_bold_font.c)
set(GEN_INCONSOLATA_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_inconsolata_font.c)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/nerd-fonts-symbols-only/SymbolsNerdFont-Regular.ttf" "${GEN_NERD_FONT}" editor_nerd_symbols_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter.ttf" "${GEN_UI_FONT}" editor_ui_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter-Bold.ttf" "${GEN_UI_BOLD_FONT}" editor_ui_bold_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter-Medium.ttf" "${GEN_UI_MEDIUM_FONT}" editor_ui_medium_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter-SemiBold.ttf" "${GEN_UI_SEMIBOLD_FONT}" editor_ui_semibold_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter-Numeric.ttf" "${GEN_UI_NUMERIC_FONT}" editor_ui_numeric_font)
# Studio's embedded faces have the same metrics on desktop and web.
foreach(face Regular Medium SemiBold Bold Numeric)
    string(TOLOWER "${face}" key)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/studio_${key}_font.c")
    chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/geist/Geist-${face}.ttf" "${generated}" "studio_${key}_font")
    list(APPEND GEN_STUDIO_FONTS "${generated}")
    list(APPEND STUDIO_FONT_TARGETS "generate_studio_${key}_font")
endforeach()
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/noto-sans/NotoSans-Regular.ttf" "${GEN_NOTO_SANS_FONT}" editor_noto_sans_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/noto-sans/NotoSans-SemiBold.ttf" "${GEN_NOTO_SANS_SEMIBOLD_FONT}" editor_noto_sans_semibold_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/noto-sans/NotoSans-Bold.ttf" "${GEN_NOTO_SANS_BOLD_FONT}" editor_noto_sans_bold_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inconsolata/Inconsolata-Regular.ttf" "${GEN_INCONSOLATA_FONT}" editor_inconsolata_font)

function(chibi_registry output)
    set(OUTPUT "${output}")
    set(PACKS "${ARGN}")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/compose_registry.cmake")
endfunction()

# --- reflect tool -----------------------------------------------------------
# Reflection runs during the build, so it must always be a native host
# executable. A cross build (notably Emscripten) cannot link libclang into WASM
# or execute a WASM reflect tool during configuration/build.
set(CHIBI_REFLECT_EXECUTABLE "" CACHE FILEPATH
    "Native reflect executable used when cross-compiling")
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if(NOT CHIBI_REFLECT_EXECUTABLE OR
       NOT EXISTS "${CHIBI_REFLECT_EXECUTABLE}")
        message(FATAL_ERROR
            "Emscripten builds require CHIBI_REFLECT_EXECUTABLE set to a native reflect tool")
    endif()
    set(CHIBI_REFLECT_COMMAND "${CHIBI_REFLECT_EXECUTABLE}")
    set(CHIBI_REFLECT_DEPENDENCY "${CHIBI_REFLECT_EXECUTABLE}")
else()
    add_executable(reflect tools/reflect/reflect.c tools/reflect/collect.c tools/reflect/identity.c tools/reflect/emit.c tools/reflect/emit_function.c tools/reflect/emit_component.c src/support/xalloc.c)
    target_include_directories(reflect PRIVATE ${LIBCLANG_INCLUDE_DIR})
    target_link_libraries(reflect PRIVATE ${LIBCLANG_LIBRARY})
    get_filename_component(LIBCLANG_LIBRARY_DIR ${LIBCLANG_LIBRARY} DIRECTORY)
    set_target_properties(reflect PROPERTIES BUILD_RPATH ${LIBCLANG_LIBRARY_DIR})
    set(CHIBI_REFLECT_COMMAND $<TARGET_FILE:reflect>)
    set(CHIBI_REFLECT_DEPENDENCY reflect)
endif()

# --- generated registry ---
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(BASE_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/base_registry.c)
# Pack order is the catalog and lookup order.
set(BASE_REGISTRY_PACKS
    SHADER_PACK
    MODEL_PACK
    FIELD_NODE_PACK
    CORE_EVENT_PACK
    CORE_EXTRA_PACK
    CORE_FLOW_PACK
    CORE_ARRAY_PACK
    CORE_STATE_PACK
    CORE_SWITCH_PACK
    CORE_REROUTE_PACK
    CORE_SET_MAP_PACK
    CORE_STRUCT_PACK
    CORE_COMPONENT_PACK
)
chibi_registry("${BASE_REGISTRY}" ${BASE_REGISTRY_PACKS})

# --- graph model and execution ---

if(NOT CHIBI_DEPENDENCY_PACKAGE)
    include(cmake/ckdl.cmake)
    include(cmake/thirdparty.cmake)
endif()

add_library(core STATIC
    ${CORE_SOURCES}

)
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt
)
target_link_libraries(core PRIVATE ckdl chibi_thirdparty)
if(UNIX)
    target_link_libraries(core PUBLIC m)
endif()
if(CHIBI_ENABLE_SJLIT)
    target_compile_definitions(core PRIVATE CHIBI_HAVE_SJLIT=1)
    target_link_libraries(core PRIVATE chibi_sljit)
else()
    target_compile_definitions(core PRIVATE CHIBI_HAVE_SJLIT=0)
endif()

# --- headless ECS consumer module -------------------------------------------

add_library(ecs STATIC ${ECS_SOURCES})
target_include_directories(ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecs PUBLIC core)

# --- generated runtime_support component pack ------------------------------------------
set(GAME_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/game_registry.c)
set(GAME_COMPONENT_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/component_ids.tsv)
set(GAME_REFLECT_ARGS --include ${CMAKE_CURRENT_SOURCE_DIR}/src --include ${CMAKE_CURRENT_SOURCE_DIR} --manifest ${GAME_COMPONENT_MANIFEST} --include ${cute_framework_SOURCE_DIR}/include)
if(LIBCLANG_RESOURCE_INCLUDE_DIR)
    list(APPEND GAME_REFLECT_ARGS --include ${LIBCLANG_RESOURCE_INCLUDE_DIR})
endif()
if(CHIBI_SDKROOT)
    list(APPEND GAME_REFLECT_ARGS --sysroot ${CHIBI_SDKROOT})
endif()
add_custom_command(
    OUTPUT ${GAME_REGISTRY}.stamp
    BYPRODUCTS ${GAME_REGISTRY}
    COMMAND ${CHIBI_REFLECT_COMMAND}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/game_nodes.c
            ${GAME_REGISTRY}
            GAME
            ${GAME_REFLECT_ARGS}
    COMMAND ${CMAKE_COMMAND} -E touch ${GAME_REGISTRY}.stamp
    DEPENDS ${CHIBI_REFLECT_DEPENDENCY}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/game_nodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/interface.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/components.h
            ${GAME_COMPONENT_MANIFEST}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/model/annotate.h
    COMMENT "Generating the built-in component reflection pack"
    VERBATIM
)
add_custom_target(generate_runtime_registry DEPENDS ${GAME_REGISTRY}.stamp)
add_library(runtime_support STATIC ${RUNTIME_SUPPORT_SOURCES} ${GAME_REGISTRY})
add_dependencies(runtime_support generate_runtime_registry)
target_include_directories(runtime_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
)
target_link_libraries(runtime_support PUBLIC ecs core cute)
chibi_enable_tracy(runtime_support)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_THREADS)
    target_link_libraries(runtime_support PRIVATE enkiTS)
    target_compile_definitions(runtime_support PRIVATE CHIBI_HAS_ENKITS=1)
endif()

# --- headless automation controller ----------------------------------------

add_library(authoring_graph STATIC src/automation/graph_edit.c)
target_include_directories(authoring_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(authoring_graph PUBLIC runtime_support core)

add_library(automation STATIC src/automation/controller.c src/automation/field_preview.c
    src/automation/graph_optimize.c src/automation/navigation.c
    src/automation/prototype_edit.c src/automation/scene_edit.c src/automation/asset_write.c)
target_include_directories(automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(automation PUBLIC runtime_support core authoring_graph editor)

# --- material rasterization and radiance transport --------------------------
add_library(chibi_render STATIC src/render/fog.c src/render/material_raster.c src/render/fluid_raster.c src/render/reflections.c src/render/radiance.c src/render/post_2d.c src/render/gpu_timing.c
    src/render/geometry_2d.c
    src/render/mesh_surface.c src/render/mesh_material.c
    src/render/arena3d.c src/render/arena_geometry.c src/render/arena_influence.c
    src/render/arena_parts.c src/render/arena_visual.c src/render/arena_trail.c
    src/render/arena_camera.c src/render/arena_pick.c src/render/arena_post.c
    src/render/ui.c)
target_include_directories(chibi_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chibi_render PUBLIC runtime_support core cute chibi_ui)
if(APPLE)
    enable_language(OBJC)
    target_sources(chibi_render PRIVATE src/render/gpu_clock.m)
    target_link_libraries(chibi_render PUBLIC "-framework Metal" "-framework Foundation")
else()
    target_sources(chibi_render PRIVATE src/render/gpu_clock.c)
endif()
target_compile_options(chibi_render PRIVATE -Wno-macro-redefined)
chibi_enable_tracy(chibi_render)
set_source_files_properties(src/render/scene_renderer.c PROPERTIES COMPILE_OPTIONS -Wno-macro-redefined)

# --- host frame loop and optional viewport surface --------------------------

add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine PUBLIC runtime_support ecs core cute)
target_compile_options(engine PRIVATE -Wno-macro-redefined)
chibi_enable_tracy(engine)

# --- editor UI (RmlUi Studio) ---

include(cmake/rmlui.cmake)
add_library(editor STATIC ${EDITOR_SOURCES})
target_include_directories(editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor PRIVATE runtime_support core cute authoring_graph chibi_rmlui)

# --- app: the minimal ECS movement editor -------------------------------
set(APP_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/app_registry.c)
set(APP_REGISTRY_PACKS
    SHADER_BINDING_PACK
    SHADER_PACK
    MODEL_PACK
    FIELD_NODE_PACK
    CORE_EVENT_PACK
    GAME_MATERIAL_PACK
    GAME_ENTITY_PACK
    GAME_SAVE_PACK
    GAME_AUDIO_PACK
    GAME_UI_PACK
    GAME_VISIBILITY_PACK
    CORE_EXTRA_PACK
    CORE_FLOW_PACK
    CORE_ARRAY_PACK
    CORE_STATE_PACK
    CORE_SWITCH_PACK
    CORE_REROUTE_PACK
    CORE_SET_MAP_PACK
    CORE_STRUCT_PACK
    CORE_COMPONENT_PACK
)
chibi_registry("${APP_REGISTRY}" ${APP_REGISTRY_PACKS} GAME_REGISTRY_PACK)


function(chibi_watch_platform target)
    target_sources(${target} PRIVATE src/host/process.c src/host/watch.c src/support/xalloc.c)
    find_package(Threads REQUIRED)
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(APPLE)
        target_sources(${target} PRIVATE src/host/watch_macos.c)
        target_link_libraries(${target} PRIVATE "-framework CoreServices")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(${target} PRIVATE src/host/watch_linux.c)
    else()
        message(FATAL_ERROR "Development source watching supports macOS and Linux")
    endif()
endfunction()

set(HOST_SOURCES src/host/main.c src/host/module.c src/host/state_file.c src/host/stacktrace.c)
add_library(chibi_sources OBJECT ${APP_SOURCES} ${APP_REGISTRY})
target_include_directories(chibi_sources PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(chibi_sources PRIVATE chibi_render editor engine runtime_support cute)
chibi_enable_tracy(chibi_sources)
if(CHIBI_DEVELOPMENT_RELOAD AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    foreach(project_target core ecs runtime_support engine editor chibi_render)
        set_target_properties(${project_target} PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
    endforeach()
    if(TARGET chibi_sljit)
        set_target_properties(chibi_sljit PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
    endif()
    set_target_properties(chibi_sources PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(chibi_dll SHARED $<TARGET_OBJECTS:chibi_sources>)
    target_link_libraries(chibi_dll PRIVATE chibi_render editor engine runtime_support cute)
    chibi_enable_tracy(chibi_dll)
    set_target_properties(chibi_dll PROPERTIES C_VISIBILITY_PRESET hidden LINKER_LANGUAGE CXX)
    add_executable(chibi ${HOST_SOURCES} src/host/dev.c src/support/resources.c)
    chibi_watch_platform(chibi)
    target_compile_definitions(chibi PRIVATE CHIBI_DEVELOPMENT_HOST=1)
    target_include_directories(chibi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson)
    target_include_directories(chibi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(chibi PRIVATE cute chibi_thirdparty ${CMAKE_DL_LIBS})
    chibi_enable_tracy(chibi)
    set_target_properties(chibi PROPERTIES LINKER_LANGUAGE CXX)
else()
    add_executable(chibi ${HOST_SOURCES} $<TARGET_OBJECTS:chibi_sources>)
    target_include_directories(chibi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(chibi PRIVATE CHIBI_STATIC_APPLICATION=1)
    target_link_libraries(chibi PRIVATE ${CMAKE_DL_LIBS} chibi_render editor engine runtime_support cute)
    chibi_enable_tracy(chibi)
    set_target_properties(chibi PROPERTIES LINKER_LANGUAGE CXX)
endif()

add_executable(chibi_ctl tools/chibi_ctl/main.c ${APP_REGISTRY})
add_dependencies(chibi_ctl generate_runtime_registry)
target_include_directories(chibi_ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chibi_ctl PRIVATE automation chibi_render editor engine)
set_target_properties(chibi_ctl PROPERTIES LINKER_LANGUAGE CXX)

target_sources(chibi_ctl PRIVATE src/automation/render_preview.c
    src/automation/scene_preview.c src/render/scene_renderer.c
    src/render/shader_preview.c src/render/shader_fragment.c)
if(APPLE)
    enable_language(OBJC)
    target_sources(chibi_ctl PRIVATE src/automation/gpu_capture.m)
    target_compile_options(chibi_ctl PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-Wno-elaborated-enum-base>)
    target_link_libraries(chibi_ctl PRIVATE "-framework Metal" "-framework Foundation")
else()
    target_sources(chibi_ctl PRIVATE src/automation/gpu_capture.c)
endif()
chibi_enable_tracy(chibi_ctl)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Under Node the controller reads projects from disk, so the same headless
    # scenarios run natively and as WASM (make profile-compare).
    target_link_options(chibi_ctl PRIVATE "-sENVIRONMENT=node" "-sNODERAWFS=1" "-sEXIT_RUNTIME=1")
endif()

# Authored assets must stay in the canonical form chibi_ctl --format writes.
set(CHIBI_ASSET_ROOTS
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/projects/arena2d
    ${CMAKE_SOURCE_DIR}/projects/SpaceCadet
    ${CMAKE_SOURCE_DIR}/projects/hrc_lab
    ${CMAKE_SOURCE_DIR}/projects/sdf_lab
    ${CMAKE_SOURCE_DIR}/projects/modeling_lab
    ${CMAKE_SOURCE_DIR}/projects/studio
    ${CMAKE_SOURCE_DIR}/test/fixtures
    ${CMAKE_SOURCE_DIR}/test/fixtures/studio)
set(CHIBI_ASSET_CHECKS)
foreach(asset_root ${CHIBI_ASSET_ROOTS})
    list(APPEND CHIBI_ASSET_CHECKS COMMAND ${CMAKE_COMMAND} -E env MallocNanoZone=0
        $<TARGET_FILE:chibi_ctl> --project-root ${asset_root} --format --check)
endforeach()
add_custom_target(check-assets ${CHIBI_ASSET_CHECKS} DEPENDS chibi_ctl VERBATIM)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_custom_target(check-mcp
        COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_SOURCE_DIR}/test/mcp_horizon_options_test.mjs
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl> CHIBI_MCP_SKIP_GPU=1
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/authoring-smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/material-smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/optimizer-smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/design-smoke.mjs ${CMAKE_SOURCE_DIR}
        DEPENDS chibi_ctl
        VERBATIM)
    add_custom_target(check-mcp-gpu
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/authoring-smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl> CHIBI_MCP_GPU=1
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/design-smoke.mjs ${CMAKE_SOURCE_DIR}
        DEPENDS chibi_ctl
        VERBATIM)
else()
    add_custom_target(check-mcp
        COMMAND ${CMAKE_COMMAND} -E echo "Node.js not found; skipping MCP protocol check"
        VERBATIM)
    add_custom_target(check-mcp-gpu
        COMMAND ${CMAKE_COMMAND} -E echo "Node.js not found; skipping MCP GPU check"
        VERBATIM)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(CHIBI_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/tools/web_shell.html")
    if(CHIBI_WEB_THREADS)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tools/web_shell.html)
        file(READ "${CHIBI_WEB_SHELL}" CHIBI_THREADED_SHELL)
        # Emscripten requires its entry placeholder. Keep the generated entry
        # inert until the isolation bootstrap can safely load the application.
        string(REPLACE "{{{ SCRIPT }}}" "<template>{{{ SCRIPT }}}</template><script src=\"chibi-isolation.js\"></script>"
            CHIBI_THREADED_SHELL "${CHIBI_THREADED_SHELL}")
        set(CHIBI_WEB_SHELL "${CMAKE_CURRENT_BINARY_DIR}/web_shell_threads.html")
        file(WRITE "${CHIBI_WEB_SHELL}" "${CHIBI_THREADED_SHELL}")
        configure_file(tools/web_isolation.js chibi-isolation.js COPYONLY)
    else()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tools/web_shell.html)
        file(READ "${CHIBI_WEB_SHELL}" CHIBI_SERIAL_SHELL)
        string(REPLACE "{{{ SCRIPT }}}" "<template>{{{ SCRIPT }}}</template><script src=\"chibi-default.js\"></script>"
            CHIBI_SERIAL_SHELL "${CHIBI_SERIAL_SHELL}")
        set(CHIBI_WEB_SHELL "${CMAKE_CURRENT_BINARY_DIR}/web_shell_serial.html")
        file(WRITE "${CHIBI_WEB_SHELL}" "${CHIBI_SERIAL_SHELL}")
        configure_file(tools/web_default.js chibi-default.js COPYONLY)
    endif()
    string(JSON CHIBI_COLLAB_CONFIG SET "{}" roomUrl "\"${CHIBI_COLLAB_URL}\"")
    configure_file(tools/collab/config.js.in "${CMAKE_CURRENT_BINARY_DIR}/collab_config.js" @ONLY)
    set_target_properties(chibi PROPERTIES SUFFIX ".html")
    file(GLOB_RECURSE WEB_ASSETS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/content/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d/content/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/projects/modeling_lab/content/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/rmlui/resources/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*")
    set_property(TARGET chibi APPEND PROPERTY LINK_DEPENDS
        ${WEB_ASSETS}
        "${CMAKE_CURRENT_SOURCE_DIR}/project.kdl"
        "${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d/project.kdl"
        "${CMAKE_CURRENT_SOURCE_DIR}/projects/modeling_lab/project.kdl"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/web_storage.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/collab/storage.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/web_launch.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/web_frames.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/collab/client.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/collab/config.js.in"
        "${CMAKE_CURRENT_BINARY_DIR}/collab_config.js"
        "${CHIBI_WEB_SHELL}")
    target_link_options(chibi PRIVATE
        "--shell-file=${CHIBI_WEB_SHELL}"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/web_storage.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/collab/storage.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/web_launch.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/web_frames.js"
        "--pre-js=${CMAKE_CURRENT_BINARY_DIR}/collab_config.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/collab/client.js"
        "-lidbfs.js"
        "-sEXPORTED_FUNCTIONS=['_main','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','stringToNewUTF8']"
        # Each sample is a self-contained project folder.
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/project.kdl@projects/arena3d/project.kdl"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/content@projects/arena3d/content"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d/project.kdl@projects/arena2d/project.kdl"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d/content@projects/arena2d/content"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/modeling_lab/project.kdl@projects/modeling_lab/project.kdl"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/modeling_lab/content@projects/modeling_lab/content"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/src/editor/rmlui/resources@${CMAKE_CURRENT_SOURCE_DIR}/src/editor/rmlui/resources"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/studio@projects/studio"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders@${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
    )
endif()

# --- test registry: core and runtime_support packs for the consolidated suite ---
set(TEST_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/test_registry.c)
chibi_registry("${TEST_REGISTRY}" WHOLE_ALIAS_PACK WHOLE_VALUE_PACK WHOLE_MANAGED_PACK WHOLE_SEARCH_PACK ${APP_REGISTRY_PACKS} GAME_REGISTRY_PACK)

# --- test suite: consolidated headless regression checks --------------------
file(GLOB_RECURSE TEST_CONCEPT_SOURCES CONFIGURE_DEPENDS
    test/graph/*.c test/ecs/*.c test/runtime/*.c test/editor/*.c test/support/*.c)
add_executable(test_suite
    test/model_sampled.c
    test/automation_preview.c
    test/asset_value.c test/asset_source.c test/kdl_text.c
    test/collab_protocol.c test/collab_replay.c
    ${TEST_CONCEPT_SOURCES}
    test/automation_asset_write.c test/automation_refactor.c test/automation_graph_edit.c test/automation_search.c test/automation_navigation.c test/automation_graph_optimize.c test/automation_prototype_edit.c test/automation_scene_edit.c test/test_suite.c test/host_wait.c test/entities.c test/projectile.c test/system_host_wait.c test/combat.c test/targeting.c test/pursuit.c test/vector.c test/arena.c test/number_serialization.c test/auto_weapon.c test/graph_navigation.c test/extract.c test/component_conversion.c test/field_outputs.c test/function_wait.c test/function_fields.c test/enemy_attack.c test/particles.c test/labels.c test/appearance.c
    test/arena3d.c
    test/arena_input.c
    test/physics.c
    test/dash.c
    test/encounter.c
    test/encounter_query_benchmark.c
    test/system_arithmetic.c
    test/system_nodes.c
    test/arena_play.c
    test/editor_checkpoint.c
    test/math_expression.c
    test/behavior_math.c
    test/surface.c
    test/arena_post.c
    test/content_catalog.c
    test/reference_index.c
    test/refactor.c
    test/shader_effects.c
    test/shader_program.c
    test/shader_image.c
    test/shader_image_routes.c
    test/shader_graph.c
    test/shader_detail.c
    test/shader_detail_gpu.c
    test/modeling.c
    test/model_surface.c
    test/model_reuse.c
    test/model_parameters.c
    test/model_profiles.c
    test/model_materials.c
    test/model_material_scope.c
    test/model_validation.c
    test/model_inspection.c
    test/model_export.c
    test/model_bake.c
    test/model_constructive.c
    test/typed_events.c
    test/scene_instances.c
    src/render/screenshot.c
    src/render/shader_fragment.c
    src/assets/behavior_loader.c
    src/assets/project.c
    src/editor/workspace/scene_authoring.c
    src/editor/workspace/asset_index.c
    src/editor/workspace/asset_refactor.c
    src/editor/workspace/scene_actors.c
    src/editor/workspace/scene_runtime.c
    src/editor/workspace/scene_tabs.c
    src/editor/workspace/scene_viewport.c
    src/editor/workspace/model_preview.c
    src/editor/documents/scene_disk.c
    src/editor/documents/scene_document.c
    src/editor/documents/scene_documents.c
    src/editor/documents/scene_prototypes.c
    src/editor/workspace/scene_checkpoint.c
    src/editor/documents/component_properties.c
    src/editor/documents/scene_history.c
    src/editor/documents/scene_retention.c
    src/editor/workspace/scene_debug.c
    src/editor/workspace/gameplay_input.c
    src/editor/workspace/game_presentation.c
    src/editor/workspace/scenario_playback.c
    test/scene_history.c
    test/scene_checkpoint.c
    test/scene_disk.c
    test/scene_refactor.c
    test/tree_watch.c
    test/rcss.c
    test/appearance_geometry.c
    test/retained_components.c
    test/editor_flow.c
    test/component_fields.c
    test/editor_documents.c
    test/scene_debug.c
    test/orientation.c
    test/movement_input.c
    test/movement_trail.c
    test/sdf_motion.c
    src/render/sdf_shapes.c
    src/render/scene_renderer.c
    test/material.c
    test/wave.c
    test/radiance.c
    test/material_sampling.c
    test/mesh_material.c
    test/material_raster.c
    test/sdf_material.c
    test/sdf_quality.c
    test/sdf_visibility.c
    test/camera_orbit.c
    test/camera_centered_orbit.c
    test/camera_emitter_orbit.c
    test/camera_plane.c
    test/surface_gather.c
    test/surface_depth.c
    test/sdf_spatial.c
    test/mesh_surface.c
    test/mesh_asset.c
    test/radiance_history.c
    test/radiance_intervals.c
    test/interior_border.c
    test/png_encoding.c
    test/sdf_coverage.c
    test/draw_backend.cpp
    src/host/stacktrace.c
    ${TEST_REGISTRY}
)
# Generated-C system benchmarks call the same runtime helpers as SLJIT.
set_target_properties(test_suite PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(test_suite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_suite PRIVATE automation engine runtime_support ecs editor chibi_render)
if(NOT CHIBI_ENABLE_PERFORMANCE_TESTS)
    target_compile_definitions(test_suite PRIVATE CHIBI_SKIP_PERFORMANCE_TESTS=1)
endif()

# --- activation benchmark: headless per-instance event dispatch -------------
set(BENCH_BEHAVIOR_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/bench_behavior_registry.c)
chibi_registry("${BENCH_BEHAVIOR_REGISTRY}" WHOLE_VALUE_PACK ${BASE_REGISTRY_PACKS})
add_executable(bench_behavior test/bench_behavior.c test/bench_integer.c test/bench_aggregate.c test/support/whole_values.c test/support/whole_components.c ${BENCH_BEHAVIOR_REGISTRY})
target_include_directories(bench_behavior PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_behavior PRIVATE ecs core ${CMAKE_DL_LIBS})
target_compile_options(bench_behavior PRIVATE -UNDEBUG -ffp-contract=off)

add_executable(bench_scheduler test/bench_scheduler.c ${BASE_REGISTRY})
target_link_libraries(bench_scheduler PRIVATE core)
target_compile_options(bench_scheduler PRIVATE -UNDEBUG)

add_executable(bench_activation test/bench_activation.c ${BASE_REGISTRY})
target_include_directories(bench_activation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_activation PRIVATE core)

add_executable(system_benchmark test/system_benchmark.c ${APP_REGISTRY})
target_include_directories(system_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(system_benchmark PRIVATE runtime_support ecs)

add_executable(query_benchmark test/query_benchmark.c ${APP_REGISTRY})
target_include_directories(query_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(query_benchmark PRIVATE runtime_support ecs)

set_target_properties(query_benchmark PROPERTIES LINKER_LANGUAGE CXX)

add_executable(contact_benchmark test/contact_benchmark.c ${APP_REGISTRY})
target_link_libraries(contact_benchmark PRIVATE runtime_support ecs)
set_target_properties(contact_benchmark PROPERTIES LINKER_LANGUAGE CXX)

add_executable(material_thermal_benchmark test/material_thermal_benchmark.c ${APP_REGISTRY})
target_link_libraries(material_thermal_benchmark PRIVATE runtime_support ecs)
target_compile_options(material_thermal_benchmark PRIVATE -UNDEBUG -ffp-contract=off)
set_target_properties(material_thermal_benchmark PROPERTIES LINKER_LANGUAGE CXX)
add_executable(material_solver_benchmark test/material_solver_benchmark.c ${APP_REGISTRY})
target_link_libraries(material_solver_benchmark PRIVATE runtime_support ecs)
target_compile_options(material_solver_benchmark PRIVATE -UNDEBUG -ffp-contract=off)
set_target_properties(material_solver_benchmark PROPERTIES LINKER_LANGUAGE CXX)

file(
    CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        RESULT CHIBI_COMPILE_COMMANDS_LINK_RESULT
    SYMBOLIC
)
if(NOT CHIBI_COMPILE_COMMANDS_LINK_RESULT STREQUAL "0" AND
   (NOT IS_SYMLINK "${CMAKE_SOURCE_DIR}/compile_commands.json" OR
    NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json"))
    message(FATAL_ERROR
        "Could not publish the compile commands link: ${CHIBI_COMPILE_COMMANDS_LINK_RESULT}")
endif()

# Exercise the actual native loader with separately compiled generations.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    foreach(fixture reload_one reload_two reload_bad reload_reject reload_checkpoint)
        add_library(${fixture} MODULE test/reload/fixture.c)
        target_include_directories(${fixture} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set_target_properties(${fixture} PROPERTIES C_VISIBILITY_PRESET hidden)
    endforeach()
    add_library(reload_no_entry MODULE test/reload/no_entry.c)
    set_target_properties(reload_no_entry PROPERTIES C_VISIBILITY_PRESET hidden)
    target_compile_definitions(reload_checkpoint PRIVATE FAIL_CHECKPOINT=1)
    target_compile_definitions(reload_two PRIVATE GENERATION=2)
    target_compile_definitions(reload_bad PRIVATE BAD_ABI=1)
    target_compile_definitions(reload_reject PRIVATE REJECT_IMPORT=1)
    add_executable(test_reload test/reload/loader.c src/host/module.c src/host/state_file.c)
    target_include_directories(test_reload PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_reload PRIVATE ${CMAKE_DL_LIBS})
    add_custom_target(check-reload
        COMMAND test_reload $<TARGET_FILE:reload_one> $<TARGET_FILE:reload_two>
            $<TARGET_FILE:reload_bad> $<TARGET_FILE:reload_reject>
            $<TARGET_FILE:reload_no_entry> $<TARGET_FILE:reload_checkpoint>
        DEPENDS test_reload reload_one reload_two reload_bad reload_reject reload_no_entry reload_checkpoint
        VERBATIM)
endif()

if(TARGET chibi_dll)
    include(cmake/dev_config.cmake)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(test_dev test/dev/main.c test/dev/watch.c test/dev/process.c test/dev/controller.c
        src/host/dev.c src/host/state_file.c third_party/yyjson/yyjson.c)
    target_include_directories(test_dev PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson)
    chibi_watch_platform(test_dev)
    add_custom_target(check-dev COMMAND test_dev DEPENDS test_dev VERBATIM)
    find_program(CHIBI_NINJA_EXECUTABLE ninja)
    if(CHIBI_NINJA_EXECUTABLE)
        add_custom_command(TARGET check-dev POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/ninja-retry-test
                -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
                -DTEST_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/dev_ninja_retry.cmake
            VERBATIM)
    endif()
    add_executable(concurrent_runner test/dev/concurrent.c src/host/process.c)
    add_custom_target(check-generation
        COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_BINARY_DIR}/tests/generation"
            "-DCONCURRENT=$<TARGET_FILE:concurrent_runner>"
            -P "${CMAKE_SOURCE_DIR}/cmake/tests/generation.cmake"
        DEPENDS concurrent_runner VERBATIM)
    add_custom_target(check-reflect
        COMMAND ${CMAKE_COMMAND} "-DREFLECT=$<TARGET_FILE:reflect>"
            "-DTEST_DIR=${CMAKE_BINARY_DIR}/tests/identity"
            "-DCONCURRENT=$<TARGET_FILE:concurrent_runner>"
            -P "${CMAKE_SOURCE_DIR}/cmake/tests/identity.cmake"
        DEPENDS reflect concurrent_runner VERBATIM)
endif()

# Test drivers use assertions for fixture setup and verification. Keep them
# active when measuring optimized runtime libraries, which retain their normal
# Release flags. Otherwise a Release review silently skips the workload.
foreach(driver test_suite test_reload test_dev concurrent_runner bench_activation
               system_benchmark query_benchmark contact_benchmark)
    if(TARGET ${driver})
        target_compile_options(${driver} PRIVATE -UNDEBUG)
    endif()
endforeach()

# Backend policy smoke target also runs under Node in the Emscripten build.
add_executable(behavior_backend_smoke test/behavior_backend_smoke.c test/support/instance_bank.c test/support/whole_retrigger.c test/support/whole_multigate.c test/support/whole_state.c test/support/whole_switch.c test/support/whole_recursion.c test/support/whole_loops.c test/support/whole_values.c test/support/whole_components.c test/support/whole_alias.c test/support/whole_stores.c test/support/whole_arrays.c test/support/whole_queries.c test/support/whole_commands.c test/support/whole_suspend.c test/support/whole_delay.c test/support/whole_batch.c test/support/bound_components.c test/support/whole_async.c test/support/whole_motion.c test/support/whole_random.c test/support/whole_data.c test/support/whole_particles.c test/support/whole_managed.c test/support/whole_owned_arrays.c test/support/whole_array_ops.c test/support/value_index.c test/support/whole_names.c test/support/whole_search.c src/graph/nodes/vector_nodes.c src/graph/nodes/particle_nodes.c src/render/particles.c)
target_include_directories(behavior_backend_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${cute_framework_SOURCE_DIR}/include)
target_link_libraries(behavior_backend_smoke PRIVATE ecs core)
target_compile_options(behavior_backend_smoke PRIVATE -UNDEBUG)
add_executable(runtime_backend_smoke test/runtime_backend_smoke.c test/physics.c test/support/task_pool.c test/support/physics_executor.c test/support/whole_reaction.c test/support/whole_rhythm.c test/support/whole_reflected.c test/support/pending.c test/support/entity_query.c)
target_link_libraries(runtime_backend_smoke PRIVATE runtime_support)
target_compile_options(runtime_backend_smoke PRIVATE -UNDEBUG)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(behavior_backend_smoke PRIVATE --profiling-funcs "-sSTACK_OVERFLOW_CHECK=2" "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1")
    target_link_options(runtime_backend_smoke PRIVATE --profiling-funcs "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1")
    find_program(CHIBI_NODE_EXECUTABLE node REQUIRED)
    add_custom_target(check-behavior-backend
        COMMAND "${CHIBI_NODE_EXECUTABLE}" "$<TARGET_FILE:behavior_backend_smoke>"
        COMMAND "${CHIBI_NODE_EXECUTABLE}" "$<TARGET_FILE:runtime_backend_smoke>"
        DEPENDS behavior_backend_smoke runtime_backend_smoke VERBATIM)
else()
    add_custom_target(check-behavior-backend
        COMMAND "$<TARGET_FILE:behavior_backend_smoke>"
        COMMAND "$<TARGET_FILE:runtime_backend_smoke>"
        DEPENDS behavior_backend_smoke runtime_backend_smoke VERBATIM)
endif()

# The same material/field/schedule contracts run natively and in Web bytecode.
add_executable(material_engine_smoke test/material_engine_smoke.c
    test/runtime/materials.c test/runtime/material_reactions.c test/runtime/material_thermal.c test/runtime/material_detail.c
    test/runtime/material_mpm.c test/runtime/material_obstacles.c test/runtime/material_particle_index.c test/runtime/material_gravity.c test/runtime/material_simulation.c
    test/runtime/material_constitutive.c test/runtime/material_mixed.c test/runtime/material_blast.c
    test/runtime/reference_cave.c test/runtime/fields.c
    test/runtime/system_schedule.c ${APP_REGISTRY})
target_include_directories(material_engine_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(material_engine_smoke PRIVATE engine runtime_support)
target_compile_options(material_engine_smoke PRIVATE -UNDEBUG)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(material_engine_smoke PRIVATE "-g2" "-sSTACK_SIZE=524288" "-sASSERTIONS=1" "-sSTACK_OVERFLOW_CHECK=2" "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d/content@projects/arena2d/content")
    add_custom_target(check-material-engine
        COMMAND "${CHIBI_NODE_EXECUTABLE}" "$<TARGET_FILE:material_engine_smoke>"
        DEPENDS material_engine_smoke VERBATIM)
else()
    add_custom_target(check-material-engine COMMAND material_engine_smoke
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS material_engine_smoke VERBATIM)
endif()

# UI documents (Studio UI authoring, workstream D): the test suite compiles the
# scene tabs itself, and they hold UI buffers.
target_sources(test_suite PRIVATE
    src/editor/documents/ui_buffer.c
    src/editor/collab/workspace.c
    src/editor/collab/actions.c
    src/editor/collab/actions_scene.c
)

# --- UI language service (workstream B) ---------------------------------------
# ui_source parses ui assets with tree-sitter for Studio and the MCP; ui_check
# validates one in a scratch RmlUi document; ui_edit serves the MCP's ui tools.
if(NOT CHIBI_DEPENDENCY_PACKAGE)
    include(cmake/tree_sitter.cmake)
endif()
add_library(ui_source STATIC
    src/assets/ui_source.c src/assets/ui_source_complete.c src/assets/ui_source_edit.c
    src/assets/ui_source_format.c)
target_include_directories(ui_source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_source PUBLIC runtime_support core chibi_tree_sitter)
target_link_libraries(editor PRIVATE ui_source chibi_render)
# ui_edit validates through ui_check, which lives with the RmlUi host.
target_link_libraries(automation PUBLIC ui_source chibi_render)
target_sources(automation PRIVATE src/automation/ui_edit.c)
target_sources(chibi_render PRIVATE src/render/ui_check.c)
# --- Text editor on Zep (workstream C) ---------------------------------------
# JetBrains Mono (OFL), the code editor's face, embedded as the UI faces are.
set(GEN_CODE_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_code_font.c)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf" "${GEN_CODE_FONT}" editor_code_font)
target_sources(chibi_ui PRIVATE ${GEN_CODE_FONT})
add_dependencies(chibi_ui generate_editor_code_font)
# Zep, its C binding and the kit_code primitive.
include(cmake/zep.cmake)

# --- Code intelligence in the Text Editor (workstream I) ----------------------
# Hover descriptions of ui symbols, for Studio's Text Editor and the MCP.
target_sources(ui_source PRIVATE src/assets/ui_source_hover.c)
