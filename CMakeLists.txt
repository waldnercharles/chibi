cmake_minimum_required(VERSION 3.20)
project(chibi LANGUAGES C CXX)

option(CHIBI_ENABLE_PERFORMANCE_TESTS
       "Run timing-sensitive performance checks in the test suite" OFF)

if(CMAKE_HOST_APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CHIBI_SDKROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- cute_framework (window host + dcimgui), pinned commit ---
include(FetchContent)
option(CHIBI_DEVELOPMENT_RELOAD "Build the native reload host and application DLL" OFF)
if(CHIBI_DEVELOPMENT_RELOAD AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(CF_FRAMEWORK_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(CF_FRAMEWORK_STATIC ON CACHE BOOL "" FORCE)
endif()
set(CF_CUTE_SHADERC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    cute_framework
    GIT_REPOSITORY https://github.com/RandyGaul/cute_framework.git
    GIT_TAG        a13f4661f5ba27e8d45027b52f121c1704786425
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(cute_framework)
# Apple's SDK uses fixed-underlying enum typedef declarations rejected by
# upstream Clang 22. Restrict compatibility to the framework's SDK consumers.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(cute PRIVATE -Wno-elaborated-enum-base)
endif()

# WebGL has no base-vertex draw support; large graph canvases exceed 16-bit indices.
# PUBLIC keeps ImGui, dcimgui and every consumer on the same ABI.
target_compile_definitions(cute PUBLIC IMGUI_USE_WCHAR32 "ImDrawIdx=unsigned int")
set(CUTE_SOURCE "${cute_framework_SOURCE_DIR}")
foreach(CUTE_PATCH_NAME cute-interior-border cute-custom-coverage cute-imgui-close-glyph cute-custom-shape-cache cute-imgui-render-target cute-imgui-smooth-scroll cute-draw-samplers cute-gles-ring-reuse cute-gles-buffer-streaming cute-hidden-background cute-hidden-present)
    set(CUTE_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/${CUTE_PATCH_NAME}.patch")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CUTE_PATCH}")
    include(cmake/apply_cute_patch.cmake)
endforeach()
unset(CUTE_PATCH_NAME)
unset(CUTE_SOURCE)
unset(CUTE_PATCH)

# --- SLJIT backend ----------------------------------------------------------
option(CHIBI_ENABLE_SJLIT "Build the SLJIT graph JIT backend" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # SLJIT emits native machine code into executable memory. Browsers cannot
    # execute that code from WASM linear memory; web uses bytecode instead.
    set(CHIBI_ENABLE_SJLIT OFF CACHE BOOL "Build the SLJIT graph JIT backend" FORCE)
endif()
set(CHIBI_SJLIT_SOURCE_DIR "" CACHE PATH "Use an existing SLJIT checkout")
if(CHIBI_ENABLE_SJLIT)
    if(CHIBI_SJLIT_SOURCE_DIR)
        set(SLJIT_SOURCE_DIR ${CHIBI_SJLIT_SOURCE_DIR})
    else()
        FetchContent_Declare(
            sljit
            GIT_REPOSITORY https://github.com/zherczeg/sljit.git
            GIT_TAG        a0020dd49a21020434ff838fc308626c48cd8f05
            UPDATE_DISCONNECTED TRUE
        )
        FetchContent_GetProperties(sljit)
        if(NOT sljit_POPULATED)
            FetchContent_Populate(sljit)
        endif()
        set(SLJIT_SOURCE_DIR ${sljit_SOURCE_DIR})
    endif()
    add_library(chibi_sljit STATIC ${SLJIT_SOURCE_DIR}/sljit_src/sljitLir.c)
    target_include_directories(chibi_sljit PUBLIC ${SLJIT_SOURCE_DIR}/sljit_src)
    target_compile_definitions(chibi_sljit PRIVATE SLJIT_WX_EXECUTABLE_ALLOCATOR=1)
endif()

# libclang (build tool only; runtime stays dependency-free).
set(LIBCLANG_ROOT "" CACHE PATH "LLVM installation prefix")
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm
    PATH_SUFFIXES include)
find_library(LIBCLANG_LIBRARY NAMES clang libclang clang-18 libclang-18
    HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm
    PATH_SUFFIXES lib)
file(GLOB LIBCLANG_RESOURCE_INCLUDE_DIRS
    "${LIBCLANG_ROOT}/lib/clang/*/include"
    "/opt/homebrew/opt/llvm/lib/clang/*/include"
    "/usr/local/opt/llvm/lib/clang/*/include"
)
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
option(CHIBI_ENABLE_SANITIZERS
       "Enable the selected sanitizer for native targets" ON)
set(CHIBI_SANITIZERS "address,undefined" CACHE STRING
    "Sanitizer runtime to use: address, undefined, address,undefined, or thread")
set_property(CACHE CHIBI_SANITIZERS PROPERTY STRINGS
    address undefined "address,undefined" thread)
option(CHIBI_ENABLE_CCACHE "Use ccache when it is available" ON)

# --- optional Tracy instrumentation ----------------------------------------
option(CHIBI_ENABLE_TRACY
       "Enable native Tracy profiling instrumentation" OFF)
if(CHIBI_ENABLE_TRACY)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(FATAL_ERROR "CHIBI_ENABLE_TRACY is only supported by native builds")
    endif()
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiling" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "Only collect Tracy data after connection" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "Restrict Tracy connections to localhost" FORCE)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c # v0.14.1
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(tracy)
endif()

function(chibi_enable_tracy target)
    if(CHIBI_ENABLE_TRACY)
        target_compile_definitions(${target} PRIVATE CHIBI_ENABLE_TRACY=1)
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
    endif()
endfunction()

# Native worlds share a lazy task pool. The portable runtime stays serial and
# does not fetch or link EnkiTS or require browser pthread support.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(ENKITS_BUILD_C_INTERFACE ON CACHE BOOL "" FORCE)
    set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENKITS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENKITS_SANITIZE ${CHIBI_ENABLE_SANITIZERS} CACHE BOOL "" FORCE)
    FetchContent_Declare(enkits
        GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
        GIT_TAG 4cba61a045d8e759bd35e5d85691bf9f7271f822
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(enkits)
    set_target_properties(enkiTS PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

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

if(CHIBI_ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING
            "C compiler launcher")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING
            "CXX compiler launcher")
    endif()
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
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/nerd-fonts-symbols-only/SymbolsNerdFont-Regular.ttf" "${GEN_NERD_FONT}" editor_nerd_symbols_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter.ttf" "${GEN_UI_FONT}" editor_ui_font)
chibi_embed("${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/inter/Inter-Bold.ttf" "${GEN_UI_BOLD_FONT}" editor_ui_bold_font)

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
    CORE_EVENT_PACK
    CORE_EXTRA_PACK
    CORE_FLOW_PACK
    CORE_ARRAY_PACK
    CORE_STATE_PACK
    CORE_SWITCH_PACK
    CORE_REROUTE_PACK
    CORE_SET_MAP_PACK
    CORE_STRUCT_PACK
)
chibi_registry("${BASE_REGISTRY}" ${BASE_REGISTRY_PACKS})

# --- graph model and execution ---

add_library(core STATIC
    ${CORE_SOURCES}
    third_party/yyjson/yyjson.c
    third_party/fmt/fmt.c

)
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt
)
if(UNIX)
    target_link_libraries(core PUBLIC m)
endif()
set_source_files_properties(third_party/fmt/fmt.c PROPERTIES
    COMPILE_OPTIONS "-Wno-deprecated-declarations")
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
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_libraries(runtime_support PRIVATE enkiTS)
    target_compile_definitions(runtime_support PRIVATE CHIBI_HAS_ENKITS=1)
endif()

# --- headless automation controller ----------------------------------------

add_library(automation STATIC src/automation/controller.c
    src/automation/graph_edit.c src/automation/graph_optimize.c
    src/automation/prototype_edit.c src/automation/scene_edit.c src/automation/asset_write.c)
target_include_directories(automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(automation PUBLIC runtime_support core)

# --- material rasterization and radiance transport --------------------------
add_library(chibi_render STATIC src/render/optics.c src/render/scene_optics.c src/render/medium.c src/render/fog.c src/render/material_raster.c src/render/fluid_raster.c src/render/reflections.c src/render/radiance.c
    src/render/geometry_2d.c src/render/light_field_2d.c
    src/render/arena3d.c src/render/arena_geometry.c src/render/arena_influence.c
    src/render/arena_parts.c src/render/arena_visual.c src/render/arena_trail.c
    src/render/arena_camera.c src/render/arena_pick.c src/render/arena_post.c)
target_compile_definitions(chibi_render PRIVATE CHIBI_RENDER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
target_include_directories(chibi_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chibi_render PUBLIC runtime_support core cute)
target_compile_options(chibi_render PRIVATE -Wno-macro-redefined)
chibi_enable_tracy(chibi_render)
set_source_files_properties(src/render/scene_renderer.c PROPERTIES COMPILE_OPTIONS -Wno-macro-redefined)

# Hidden native GPU optics experiments, independent of authored game content.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(gi_lab tools/gi_lab/main.c)
    target_compile_definitions(gi_lab PRIVATE GI_LAB_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools/gi_lab/shaders")
    target_link_libraries(gi_lab PRIVATE core cute)
    # The lab validates argument ranges, shader resources, and HDR readback in Release too.
    target_compile_options(gi_lab PRIVATE -UNDEBUG)
endif()

# --- host frame loop and optional viewport surface --------------------------

add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine PUBLIC runtime_support ecs core cute)
target_compile_options(engine PRIVATE -Wno-macro-redefined)
chibi_enable_tracy(engine)

# --- editor canvas (graph model + native dcimgui widget) ---

add_library(editor STATIC ${EDITOR_SOURCES} ${GEN_NERD_FONT} ${GEN_UI_FONT} ${GEN_UI_BOLD_FONT})
add_dependencies(editor generate_editor_nerd_symbols_font generate_editor_ui_font generate_editor_ui_bold_font)
target_include_directories(editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor PRIVATE runtime_support core cute)

# --- app: the minimal ECS movement editor -------------------------------
set(APP_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/app_registry.c)
set(APP_REGISTRY_PACKS
    SHADER_BINDING_PACK
    SHADER_PACK
    CORE_EVENT_PACK
    GAME_ENTITY_PACK
    GAME_SAVE_PACK
    GAME_AUDIO_PACK
    GAME_VISIBILITY_PACK
    CORE_EXTRA_PACK
    CORE_FLOW_PACK
    CORE_ARRAY_PACK
    CORE_STATE_PACK
    CORE_SWITCH_PACK
    CORE_REROUTE_PACK
    CORE_SET_MAP_PACK
    CORE_STRUCT_PACK
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
target_compile_definitions(chibi_sources PRIVATE CHIBI_RENDER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
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
    add_executable(chibi ${HOST_SOURCES} src/host/dev.c third_party/yyjson/yyjson.c)
    chibi_watch_platform(chibi)
    target_compile_definitions(chibi PRIVATE CHIBI_DEVELOPMENT_HOST=1)
    target_compile_definitions(chibi PRIVATE CHIBI_RENDER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
    target_include_directories(chibi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson)
    target_include_directories(chibi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(chibi PRIVATE cute ${CMAKE_DL_LIBS})
    chibi_enable_tracy(chibi)
    set_target_properties(chibi PROPERTIES LINKER_LANGUAGE CXX)
else()
    add_executable(chibi ${HOST_SOURCES} $<TARGET_OBJECTS:chibi_sources>)
    target_include_directories(chibi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(chibi PRIVATE CHIBI_STATIC_APPLICATION=1)
    target_compile_definitions(chibi PRIVATE CHIBI_RENDER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
    target_link_libraries(chibi PRIVATE ${CMAKE_DL_LIBS} chibi_render editor engine runtime_support cute)
    chibi_enable_tracy(chibi)
    set_target_properties(chibi PROPERTIES LINKER_LANGUAGE CXX)
endif()

add_executable(chibi_ctl tools/chibi_ctl/main.c ${APP_REGISTRY})
add_dependencies(chibi_ctl generate_runtime_registry)
target_include_directories(chibi_ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(chibi_ctl PRIVATE CHIBI_RENDER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
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

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_custom_target(check-mcp
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/authoring-smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl>
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/optimizer-smoke.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E env CHIBI_CTL=$<TARGET_FILE:chibi_ctl> CHIBI_MCP_GPU=1
            ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mcp/design-smoke.mjs ${CMAKE_SOURCE_DIR}
        DEPENDS chibi_ctl
        VERBATIM)
else()
    add_custom_target(check-mcp
        COMMAND ${CMAKE_COMMAND} -E echo "Node.js not found; skipping MCP protocol check"
        VERBATIM)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set_target_properties(chibi PROPERTIES SUFFIX ".html")
    file(GLOB_RECURSE WEB_ASSETS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/content/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/project.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*")
    set_property(TARGET chibi APPEND PROPERTY LINK_DEPENDS
        ${WEB_ASSETS}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/web_launch.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/web_shell.html")
    target_link_options(chibi PRIVATE
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/tools/web_shell.html"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/web_launch.js"
        "-sEXPORTED_RUNTIME_METHODS=['FS']"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/project.json@project.json"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/content@content"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/projects/arena2d@projects/arena2d"
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
    ${TEST_CONCEPT_SOURCES}
    test/automation_asset_write.c test/automation_graph_edit.c test/automation_graph_optimize.c test/automation_prototype_edit.c test/automation_scene_edit.c test/test_suite.c test/host_wait.c test/entities.c test/projectile.c test/system_host_wait.c test/combat.c test/targeting.c test/pursuit.c test/vector.c test/arena.c test/number_serialization.c test/auto_weapon.c test/graph_navigation.c test/extract.c test/component_conversion.c test/field_outputs.c test/function_wait.c test/function_fields.c test/flags.c test/enemy_attack.c test/particles.c test/labels.c test/appearance.c
    test/ui_scroll.c
    test/arena3d.c
    test/arena_input.c
    test/physics.c
    test/dash.c
    test/encounter.c
    test/encounter_query_benchmark.c
    test/system_arithmetic.c
    test/arena_ui.c
    test/shader_ui.c
    test/ui_keyboard.c
    test/editor_checkpoint.c
    test/gameplay_keyboard.c
    test/math_expression.c
    test/behavior_math.c
    test/surface.c
    test/arena_post.c
    test/content_catalog.c
    test/asset_index.c
    test/shader_effects.c
    test/shader_program.c
    test/shader_image.c
    test/shader_image_files.c
    test/shader_image_routes.c
    test/shader_graph.c
    test/typed_events.c
    test/navigation_runtime.c
    src/render/screenshot.c
    src/render/shader_fragment.c
    src/assets/behavior_loader.c
    src/assets/project.c
    src/editor/workspace/scene_authoring.c
    src/editor/workspace/asset_index.c
    src/editor/panels/asset_browser.c
    src/editor/panels/asset_document.c
    src/editor/workspace/scene_actors.c
    src/editor/workspace/scene_runtime.c
    src/editor/workspace/scene_tabs.c
    src/editor/workspace/scene_viewport.c
    src/editor/documents/scene_document.c
    src/editor/documents/scene_documents.c
    src/editor/workspace/scene_workspace.c
    src/editor/documents/scene_prototypes.c
    src/editor/workspace/scene_checkpoint.c
    src/editor/panels/scene_properties.c
    src/editor/panels/prototype_inspector.c
    src/editor/panels/scene_inspector.c
    src/editor/panels/scene_navigator.c
    src/editor/documents/scene_history.c
    src/editor/documents/scene_retention.c
    src/editor/workspace/prototype_still.c
    src/editor/workspace/scene_navigation.c
    src/editor/workspace/scene_debug.c
    src/editor/workspace/gameplay_input.c
    src/editor/workspace/game_presentation.c
    test/scene_history.c
    test/scene_checkpoint.c
    test/appearance_geometry.c
    test/retained_components.c
    test/editor_flow.c
    test/component_fields.c
    test/scene_navigation.c
    test/scene_debug.c
    test/orientation.c
    test/movement_input.c
    test/movement_trail.c
    test/sdf_motion.c
    src/render/sdf_shapes.c
    src/render/scene_renderer.c
    test/material.c
    test/wave.c
    test/optics.c
    test/radiance.c
    test/radiance_intervals.c
    test/radiance_medium.c
    test/interior_border.c
    test/sdf_coverage.c
    test/draw_backend.cpp
    src/host/stacktrace.c
    src/editor/graph/preview_gate.c
    ${TEST_REGISTRY}
)
# Generated-C system benchmarks call the same runtime helpers as SLJIT.
set_target_properties(test_suite PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(test_suite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_suite PRIVATE CHIBI_RENDER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
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

add_executable(scene_benchmark test/scene_benchmark.c ${BASE_REGISTRY})
target_include_directories(scene_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scene_benchmark PRIVATE runtime_support)

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

# --- explicit formatting checks -------------------------------------------
# The web toolchain's clang-format may not understand the project's native
# configuration. Formatting is a native developer check, not a web dependency.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  find_program(CLANG_FORMAT clang-format)
endif()
if(CLANG_FORMAT)
  file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/reflect/*.c ${CMAKE_CURRENT_SOURCE_DIR}/tools/reflect/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.c ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h
  )
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} --style=file -i ${FORMAT_SOURCES}
    COMMENT "Formatting C sources"
    VERBATIM)
  add_custom_target(check-format
    COMMAND ${CLANG_FORMAT} --dry-run --Werror --style=file ${FORMAT_SOURCES}
    COMMENT "Checking C source formatting"
    VERBATIM)
endif()

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
               scene_benchmark system_benchmark query_benchmark contact_benchmark)
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
    target_link_options(behavior_backend_smoke PRIVATE "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1")
    target_link_options(runtime_backend_smoke PRIVATE "-sENVIRONMENT=node" "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1")
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
