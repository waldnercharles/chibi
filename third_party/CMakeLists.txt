# Builds the third-party archive for one target and variant: the libraries
# the engine links, prebuilt, and the Cute sources the SDK reads directly.
# CI runs it (.github/workflows/third-party.yml) and publishes the installed
# tree as a release asset. Builds only download that asset
# (cmake/third_party.cmake, tools/toolchain/Build.chibi). Temporary: it goes
# with the rest of CMake once the Chibi build graph exists.
#
# The installed tree:
#   third-party.cmake              imported Chibi::vendor_* targets
#   lib/chibi-vendor/              prebuilt static libraries
#   include/chibi/vendor/          their public headers
#   share/chibi/licenses/vendor/   their licences
#   source/cute_framework/         Cute headers and decoders, with its licence
cmake_minimum_required(VERSION 3.25)
if(CHIBI_SDK_COMPILER_HOST_ONLY)
    project(chibi_compiler_dependencies LANGUAGES C)
else()
    project(chibi_third_party LANGUAGES C CXX)
endif()
if(APPLE AND NOT CHIBI_SDK_COMPILER_HOST_ONLY)
    enable_language(OBJC)
endif()
get_filename_component(CHIBI_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(CHIBI_SANITIZERS "" CACHE STRING "Sanitizers to build the libraries with: empty, address,undefined or thread")
option(CHIBI_WEB_THREADS "Build the web libraries for pthread workers" OFF)
option(CHIBI_WEB_PLAYER "Build Cute for the browser player's WebGPU backend" OFF)
# The C runtime Chibi links on Windows, so one runtime serves both.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    if(CHIBI_WEB_THREADS)
        add_compile_options(-pthread -msimd128)
    endif()
    add_compile_options(-Wno-experimental)
    add_link_options(-Wno-experimental)
elseif(CHIBI_SANITIZERS)
    add_compile_options("-fsanitize=${CHIBI_SANITIZERS}" -fno-omit-frame-pointer)
endif()

include(FetchContent)
# Checks NAME out at COMMIT, then applies NAME/<patch>.patch from this
# directory for each listed patch, in order. Sets NAME_SOURCE.
function(chibi_third_party name repository commit)
    FetchContent_Declare(chibi_${name} GIT_REPOSITORY "${repository}" GIT_TAG "${commit}"
        GIT_SUBMODULES "" SOURCE_SUBDIR chibi-no-build)
    FetchContent_MakeAvailable(chibi_${name})
    set(source "${chibi_${name}_SOURCE_DIR}")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/patched/${name}")
    if(NOT EXISTS "${stamp}")
        foreach(patch IN LISTS ARGN)
            execute_process(
                COMMAND git -C "${source}" apply "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${patch}.patch"
                RESULT_VARIABLE result ERROR_VARIABLE error)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "${name}/${patch}.patch does not apply: ${error}")
            endif()
        endforeach()
        file(WRITE "${stamp}" "${ARGN}\n")
    endif()
    set(${name}_SOURCE "${source}" PARENT_SCOPE)
endfunction()

# Patches are described in third_party/README.md.
chibi_third_party(sljit https://github.com/zherczeg/sljit.git
    a0020dd49a21020434ff838fc308626c48cd8f05 arm64-immediate-negation)
chibi_third_party(tree_sitter https://github.com/tree-sitter/tree-sitter.git
    da6fe9beb4f7f67beb75914ca8e0d48ae48d6406)
if(CHIBI_SDK_COMPILER_HOST_ONLY)
    # Only portable sources are needed to provision the native compiler runtime.
    # Reuse the archive's source layout and the pins/patches above; no desktop
    # libraries or platform archive are involved.
    set(compiler_sources "${CMAKE_CURRENT_BINARY_DIR}/compiler-sources")
    file(COPY "${sljit_SOURCE}/sljit_src" "${sljit_SOURCE}/LICENSE"
        DESTINATION "${compiler_sources}/source/sljit")
    file(COPY "${tree_sitter_SOURCE}/lib" "${tree_sitter_SOURCE}/LICENSE"
        DESTINATION "${compiler_sources}/source/tree_sitter")
    set(CHIBI_THIRD_PARTY_DIR "${compiler_sources}" PARENT_SCOPE)
    return()
endif()
chibi_third_party(cute_framework https://github.com/RandyGaul/cute_framework.git
    96e6034aa6bc44665856ee480962a9ddab0b87cc
    interior-border imgui-render-target eight-targets web-no-asyncify gles-ubo-instance-scope
    instance-buffer web-webgpu-device gles-range-draw-state sound-free-queue-music-volume
    audio-device sdlgpu-used-vertex-streams)
chibi_third_party(sdl3 https://github.com/libsdl-org/SDL.git
    a962f40bbba175e9716557a25d5d7965f134a3d3 wayland-seat-pointer)
chibi_third_party(s2n_tls https://github.com/aws/s2n-tls.git 1c9844722d9cc3beca5551c0f9bfed8aabbc22ed)
chibi_third_party(physfs https://github.com/icculus/physfs.git 9d18d36b5a5207b72f473f05e1b2834e347d8144)
chibi_third_party(box2d https://github.com/erincatto/box2d.git 8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3)
chibi_third_party(box3d https://github.com/erincatto/box3d.git
    8441b4a06d6d09dcfb0b0f704df4d847d1437b92 block-alignment)
chibi_third_party(freetype https://github.com/freetype/freetype.git 0a0221a1347e2f1e07c395263540026e9a0aa7c7)
chibi_third_party(rmlui https://github.com/mikke89/RmlUi.git
    ba95ffe8bfb6370efb2cdcca927eaad4710c5413
    color-mix radio-groups data-bindings flex-text same-style data-if-handler variable-source
    suppressed-text-font-layout variable-unit-invalidation box-shadow-blur input-events data-when
    event-propagation variable-line-height move-keeps-data data-template context-update-profile
    document-source document-layout)
chibi_third_party(enkits https://github.com/dougbinks/enkiTS.git 4cba61a045d8e759bd35e5d85691bf9f7271f822)

# --- Cute Framework, with the SDL3, s2n, Box2D, Box3D and PhysicsFS it adds ---
set(CF_FRAMEWORK_STATIC ON CACHE BOOL "" FORCE)
set(CF_CUTE_SHADERC OFF CACHE BOOL "" FORCE)
set(CF_CUTE_SYM OFF CACHE BOOL "" FORCE)
set(CF_FRAMEWORK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CF_FRAMEWORK_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(CF_BUILD_DOCSPARSER OFF CACHE BOOL "" FORCE)
# The browser player renders with WebGPU when its page provides a device
# (src/host/web/index.html) and falls back to WebGL 2 otherwise.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND CHIBI_WEB_PLAYER)
    set(CF_WEBGPU ON CACHE BOOL "" FORCE)
endif()
# Cute declares these with FetchContent; the patched checkouts stand in.
foreach(name SDL3 S2N BOX2D BOX3D PHYSFS)
    string(TOLOWER "${name}" lower)
    if(lower STREQUAL "s2n")
        set(lower s2n_tls)
    endif()
    set(FETCHCONTENT_SOURCE_DIR_${name} "${${lower}_SOURCE}")
endforeach()
# s2n appends its Findcrypto module path. Supply our dependency-complete wrapper
# first, while retaining any explicitly supplied discovery modules.
list(APPEND CMAKE_MODULE_PATH "${CHIBI_SDK_ROOT}/cmake")
add_subdirectory("${cute_framework_SOURCE}" cute_framework)
# s2n compiles in GNU C99 mode, while OpenSSL 3 headers use _Generic.
# Clang 22 diagnoses those system-header macro expansions at their call sites.
if(TARGET s2n AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(s2n PRIVATE -Wno-c11-extensions)
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
foreach(symbol
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
    target_compile_definitions(cute PRIVATE ${symbol}=cf_${symbol})
endforeach()

# --- RmlUi and its FreeType font rasterizer ----------------------------------
set(BUILD_SHARED_LIBS OFF)
foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${feature} ON CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${freetype_SOURCE}" freetype)
add_library(Freetype::Freetype ALIAS freetype)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
add_subdirectory("${rmlui_SOURCE}" rmlui)

# --- Physics, task, JIT and syntax libraries the SDK links ------------------
# They keep the flags the SDK compiled them with.
set(sdk_libraries box2d box3d tree_sitter)
# Box2D and Box3D round deterministically: no fused multiply-add. Their SIMD
# paths need Wasm SIMD, which only the threaded web build enables.
foreach(physics box2d box3d)
    file(GLOB sources "${${physics}_SOURCE}/src/*.c")
    add_library(${physics} STATIC ${sources})
    set_target_properties(${physics} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${physics} PRIVATE "${${physics}_SOURCE}/src"
        PUBLIC "${${physics}_SOURCE}/include")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${physics} PRIVATE -ffp-contract=off)
    endif()
    string(TOUPPER "${physics}" upper)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND CHIBI_WEB_THREADS)
        target_compile_options(${physics} PRIVATE -msimd128 -msse2)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_compile_definitions(${physics} PRIVATE ${upper}_DISABLE_SIMD)
    endif()
    if(UNIX)
        target_link_libraries(${physics} PRIVATE m)
    endif()
endforeach()
set_target_properties(box3d PROPERTIES C_EXTENSIONS YES)
# Box3D's internal scheduler and timers, and enkiTS, use the platform thread API.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(box3d PRIVATE Threads::Threads)
    add_library(enkits STATIC "${enkits_SOURCE}/src/TaskScheduler.cpp"
        "${enkits_SOURCE}/src/TaskScheduler_c.cpp")
    # Consumers link with the C++ driver; C-only ones still get the C++ runtime.
    set_target_properties(enkits PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE CXX)
    target_include_directories(enkits PUBLIC "${enkits_SOURCE}/src")
    target_compile_definitions(enkits PUBLIC ENKITS_TASK_PRIORITIES_NUM=3)
    target_link_libraries(enkits PRIVATE Threads::Threads)
    if(APPLE)
        target_link_libraries(enkits PRIVATE c++)
    elseif(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_link_libraries(enkits PRIVATE stdc++)
    endif()
    list(APPEND sdk_libraries enkits)
endif()
# The web SDK executes without SLJIT.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_library(sljit STATIC "${sljit_SOURCE}/sljit_src/sljitLir.c")
    target_compile_definitions(sljit PRIVATE SLJIT_WX_EXECUTABLE_ALLOCATOR=1)
    target_compile_options(sljit PRIVATE -w)
    target_include_directories(sljit PUBLIC "${sljit_SOURCE}/sljit_src")
    list(APPEND sdk_libraries sljit)
endif()
add_library(tree_sitter STATIC "${tree_sitter_SOURCE}/lib/src/lib.c")
target_include_directories(tree_sitter PRIVATE "${tree_sitter_SOURCE}/lib/src"
    PUBLIC "${tree_sitter_SOURCE}/lib/include")
target_compile_definitions(tree_sitter PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
target_compile_options(tree_sitter PRIVATE -w)

# --- The archive ---------------------------------------------------------------
set(CHIBI_VENDOR_DEPENDENCY_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/third-party.cmake")
set(CHIBI_VENDOR_DEPENDENCY_ROOT ".")
set(CHIBI_VENDOR_DEPENDENCY_SCOPE " GLOBAL")
include("${CHIBI_SDK_ROOT}/sdk/vendor_dependencies.cmake")
foreach(library cute RmlUi::Core ${sdk_libraries})
    chibi_vendor_dependency(${library} imported)
endforeach()
install(FILES "${CHIBI_VENDOR_DEPENDENCY_CONFIG}" DESTINATION .)
# Cute's math headers, single-file image and Vorbis decoders, and runtime shader
# compiler interface, which the SDK and its tests compile against directly.
set(cute_source source/cute_framework)
install(DIRECTORY "${cute_framework_SOURCE}/include" DESTINATION ${cute_source})
install(FILES "${cute_framework_SOURCE}/LICENSE" DESTINATION ${cute_source})
install(FILES "${cute_framework_SOURCE}/libraries/cute/cute_png.h"
    "${cute_framework_SOURCE}/libraries/cute/cute_jpg.h" DESTINATION ${cute_source}/libraries/cute)
install(FILES "${cute_framework_SOURCE}/libraries/stb/stb_vorbis.c"
    DESTINATION ${cute_source}/libraries/stb)
install(FILES "${cute_framework_SOURCE}/tools/cute_shader.h" DESTINATION ${cute_source}/tools)
