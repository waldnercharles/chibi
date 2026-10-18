# --- cute_framework (window host + dcimgui), pinned commit ---
include(FetchContent)
set(CF_FRAMEWORK_STATIC ON CACHE BOOL "" FORCE)
set(CF_CUTE_SHADERC OFF CACHE BOOL "" FORCE)
# The browser player renders with WebGPU when its page provides a device
# (src/host/web/index.html) and falls back to WebGL 2 otherwise.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND CHIBI_WEB_PLAYER)
    set(CF_WEBGPU ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    cute_framework
    GIT_REPOSITORY https://github.com/RandyGaul/cute_framework.git
    GIT_TAG        96e6034aa6bc44665856ee480962a9ddab0b87cc
    UPDATE_DISCONNECTED TRUE
)
FetchContent_GetProperties(cute_framework)
if(NOT cute_framework_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(cute_framework)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
include("${CHIBI_DEPENDENCY_SOURCE_ROOT}/cmake/apply_patch.cmake")
set(CUTE_PATCHES "")
foreach(name cute-interior-border cute-imgui-render-target cute-eight-targets cute-web-no-asyncify cute-gles-ubo-instance-scope cute-instance-buffer cute-web-webgpu-device cute-gles-range-draw-state cute-sound-free-queue-music-volume cute-audio-device cute-sdlgpu-used-vertex-streams cute-offset-of cute-empty-uniform-compare)
    list(APPEND CUTE_PATCHES "${CHIBI_DEPENDENCY_SOURCE_ROOT}/cmake/patches/${name}.patch")
endforeach()
chibi_apply_patches("${cute_framework_SOURCE_DIR}" ${CUTE_PATCHES})
unset(CUTE_PATCHES)
# Cute downloads SDL3 and s2n as GitHub archives, which sandboxes that allow
# only Git block. The first declaration of a name wins, so pin them as Git tags.
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(
    s2n
    GIT_REPOSITORY https://github.com/aws/s2n-tls.git
    GIT_TAG        v1.6.3
    # The CBMC verification submodule is not used by s2n's product CMake build.
    GIT_SUBMODULES ""
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
    EXCLUDE_FROM_ALL
)
# s2n appends its Findcrypto module path. Supply our dependency-complete wrapper
# first, while retaining any explicitly supplied discovery modules.
set(CHIBI_SAVED_MODULE_PATH "${CMAKE_MODULE_PATH}")
list(APPEND CMAKE_MODULE_PATH "${CHIBI_DEPENDENCY_SOURCE_ROOT}/cmake")
add_subdirectory("${cute_framework_SOURCE_DIR}" "${cute_framework_BINARY_DIR}")
set(CMAKE_MODULE_PATH "${CHIBI_SAVED_MODULE_PATH}")
unset(CHIBI_SAVED_MODULE_PATH)
# s2n compiles in GNU C99 mode, while OpenSSL 3 headers use _Generic.
# Clang 22 diagnoses those system-header macro expansions at their call sites.
if(TARGET s2n AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(s2n PRIVATE -Wno-c11-extensions)
endif()
# Cute populates SDL only for native builds without an installed SDL package.
FetchContent_GetProperties(SDL3)
if(sdl3_POPULATED)
    chibi_apply_patches("${sdl3_SOURCE_DIR}"
        "${CHIBI_DEPENDENCY_SOURCE_ROOT}/cmake/patches/sdl-wayland-seat-pointer.patch")
endif()
# Cute enables SIMD in its physics targets even for the compatibility bundle.
# Keep that bundle usable on browsers without Wasm SIMD.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND NOT CHIBI_WEB_THREADS)
    foreach(physics_target cute-box2d cute-box3d)
        get_target_property(physics_options ${physics_target} COMPILE_OPTIONS)
        list(REMOVE_ITEM physics_options -msimd128 -msse2)
        set_property(TARGET ${physics_target} PROPERTY COMPILE_OPTIONS ${physics_options})
    endforeach()
    target_compile_definitions(cute-box2d PRIVATE BOX2D_DISABLE_SIMD)
    target_compile_definitions(cute-box3d PRIVATE BOX3D_DISABLE_SIMD)
endif()

# Cute compiles the physics sources itself, bypassing their upstream CMake
# deterministic-math settings. Apply the same rounding policy to its targets.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cute-box2d PRIVATE -ffp-contract=off)
    target_compile_options(cute-box3d PRIVATE -ffp-contract=off)
endif()
# Apple's SDK uses fixed-underlying enum typedef declarations rejected by
# upstream Clang 22. Restrict compatibility to the framework's SDK consumers.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(cute PRIVATE -Wno-elaborated-enum-base)
endif()

# WebGL has no base-vertex draw support; 32-bit indices keep large ImGui draw lists drawable.
# PUBLIC keeps ImGui, dcimgui and every consumer on the same ABI.
target_compile_definitions(cute PUBLIC IMGUI_USE_WCHAR32 "ImDrawIdx=unsigned int")
# Cute bundles yyjson 0.7 under the names of third_party/yyjson (0.12). A
# static link can then pair 0.12 headers with 0.7 functions, which ignore
# per-value flags such as shortest float output. Cute keeps its copy under
# its own prefix.
set(CUTE_YYJSON_SYMBOLS
    unsafe_yyjson_equals unsafe_yyjson_mut_equals unsafe_yyjson_mut_ptr_getx
    unsafe_yyjson_mut_ptr_putx unsafe_yyjson_mut_ptr_removex
    unsafe_yyjson_mut_ptr_replacex unsafe_yyjson_ptr_getx unsafe_yyjson_str_pool_grow
    unsafe_yyjson_val_pool_grow yyjson_alc_pool_init yyjson_doc_mut_copy
    yyjson_merge_patch yyjson_mut_doc_free yyjson_mut_doc_imut_copy
    yyjson_mut_doc_mut_copy yyjson_mut_doc_new yyjson_mut_doc_set_str_pool_size
    yyjson_mut_doc_set_val_pool_size yyjson_mut_merge_patch yyjson_mut_patch
    yyjson_mut_val_imut_copy yyjson_mut_val_mut_copy yyjson_mut_val_write_file
    yyjson_mut_val_write_fp yyjson_mut_val_write_opts yyjson_mut_write_file
    yyjson_mut_write_fp yyjson_mut_write_opts yyjson_patch yyjson_read_file
    yyjson_read_fp yyjson_read_number yyjson_read_opts yyjson_val_mut_copy
    yyjson_val_write_file yyjson_val_write_fp yyjson_val_write_opts yyjson_version
    yyjson_write_file yyjson_write_fp yyjson_write_opts)
foreach(symbol IN LISTS CUTE_YYJSON_SYMBOLS)
    target_compile_definitions(cute PRIVATE ${symbol}=cf_${symbol})
endforeach()
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



include("${CMAKE_CURRENT_LIST_DIR}/enkits.cmake")
