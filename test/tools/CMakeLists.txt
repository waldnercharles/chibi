cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(chibi_tooling C)
endif()

set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "")
get_filename_component(CHIBI_TOOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(CHIBI_TEST_PROGRAM_ONLY "Build the installed program API contract dispatcher" OFF)
if(CHIBI_TEST_PROGRAM_ONLY)
    find_package(Chibi REQUIRED CONFIG COMPONENTS program core_capabilities)
    configure_file("${CHIBI_TOOL_ROOT}/third_party/pico_headers/pico_unit.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/third_party/pico_headers/pico_unit.h" COPYONLY)
    add_executable(test_tools main.c program_consumer.c)
    target_include_directories(test_tools PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_PROGRAM_ONLY=1)
    target_compile_features(test_tools PRIVATE c_std_23)
    target_compile_options(test_tools PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(test_tools PRIVATE Chibi::program Chibi::core_capabilities)
    return()
endif()
if(EMSCRIPTEN)
    enable_language(CXX)
    set(CHIBI_TEST_CUTE_SOURCE "" CACHE PATH "Isolated Cute source for browser regression")
    option(CHIBI_TEST_WEBGPU_BASELINE "Use upstream ASYNCIFY device creation" OFF)
    if(CHIBI_TEST_CUTE_SOURCE)
        set(CF_WEBGPU ON CACHE BOOL "" FORCE)
        set(CF_CUTE_SHADERC OFF CACHE BOOL "" FORCE)
        set(CF_FRAMEWORK_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory("${CHIBI_TEST_CUTE_SOURCE}" cute)
        set(CHIBI_TEST_PLATFORM cute)
    else()
        find_package(Chibi REQUIRED CONFIG)
        set(CHIBI_TEST_PLATFORM Chibi::platform)
    endif()
    add_executable(test_tools main.c)
    target_include_directories(test_tools PRIVATE "${CHIBI_TOOL_ROOT}" "${CHIBI_TOOL_ROOT}/src")
    target_compile_features(test_tools PRIVATE c_std_11)
    target_compile_options(test_tools PRIVATE -Wall -Wextra -Werror -Wno-experimental -UNDEBUG)
    target_link_libraries(test_tools PRIVATE ${CHIBI_TEST_PLATFORM})
    target_link_options(test_tools PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sSTACK_SIZE=1048576")
    set_target_properties(test_tools PROPERTIES SUFFIX ".js" LINKER_LANGUAGE CXX)
    if(CHIBI_TEST_CUTE_SOURCE)
        # These diagnostics use WebGPU APIs absent from the normal GLES SDK.
        set(CHIBI_TEST_ADOPT_DEVICE 1)
        if(CHIBI_TEST_WEBGPU_BASELINE)
            set(CHIBI_TEST_ADOPT_DEVICE 0)
            target_link_options(test_tools PRIVATE "-sASYNCIFY=1")
        endif()
        configure_file("${CHIBI_TOOL_ROOT}/test/render/webgpu_staging.html"
            "${CMAKE_CURRENT_BINARY_DIR}/webgpu_staging.html" @ONLY)
        target_sources(test_tools PRIVATE ../render/webgpu_staging.c ../draw_backend.cpp)
        target_compile_definitions(test_tools PRIVATE CHIBI_TEST_WEBGPU_STAGING=1
            CHIBI_TEST_ADOPT_DEVICE=${CHIBI_TEST_ADOPT_DEVICE})
        set_target_properties(test_tools PROPERTIES SUFFIX ".html"
            LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/webgpu_staging.html")
        target_link_options(test_tools PRIVATE
            "--shell-file=${CMAKE_CURRENT_BINARY_DIR}/webgpu_staging.html")
    elseif(TARGET Chibi::render)
        target_sources(test_tools PRIVATE audio_output.c)
        target_link_libraries(test_tools PRIVATE Chibi::render)
        target_compile_definitions(test_tools PRIVATE CHIBI_TEST_AUDIO_WEB=1)
        target_link_options(test_tools PRIVATE "-Wl,--wrap=SDL_GetTicks")
    else()
        message(FATAL_ERROR "Browser audio checks require an SDK with Chibi::render")
    endif()
    return()
endif()

option(CHIBI_INSTALLED_SDK "Link tooling contract checks against an installed SDK" OFF)
if(CHIBI_INSTALLED_SDK)
    find_package(Chibi REQUIRED CONFIG COMPONENTS behavior_runtime scene_runtime ecs execution_contract value_document)
elseif(NOT TARGET Chibi::types)
    set(CHIBI_SDK_BUILD_COMPILER OFF CACHE BOOL "Build language, compiler and source authoring packages")
    add_subdirectory("${CHIBI_TOOL_ROOT}/sdk" sdk)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/languages.cmake")
add_executable(policy_worker policy_worker.c)
target_compile_definitions(policy_worker PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
target_compile_features(policy_worker PRIVATE c_std_11)
target_compile_options(policy_worker PRIVATE -Wall -Wextra -Werror -UNDEBUG)

# The SDK contract cases can see only installed/exported SDK headers and pico_unit.
# Compiling the tests through the product's source include path would hide leaks.
configure_file("${CHIBI_TOOL_ROOT}/third_party/pico_headers/pico_unit.h"
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/third_party/pico_headers/pico_unit.h" COPYONLY)
add_library(chibi_sdk_contracts OBJECT types.c ecs.c ecs_relationship.c ecs_hierarchy.c ecs_world.c ecs_query.c ecs_query_benchmark.c)
target_include_directories(chibi_sdk_contracts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(chibi_sdk_contracts PRIVATE Chibi::execution_contract Chibi::ecs Chibi::value_document)
target_compile_options(chibi_sdk_contracts PRIVATE -Wall -Wextra -Werror -UNDEBUG)

configure_file("${CHIBI_TOOL_ROOT}/test/prepared_fixture.h"
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/prepared_fixture.h" COPYONLY)
add_library(chibi_prepared_consumer OBJECT prepared_runtime.c)
add_library(chibi_record_consumer OBJECT record_runtime.c)
target_include_directories(chibi_record_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(chibi_record_consumer PRIVATE Chibi::behavior_runtime)
target_compile_options(chibi_record_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_include_directories(chibi_prepared_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(chibi_prepared_consumer PRIVATE Chibi::behavior_runtime)
target_compile_options(chibi_prepared_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_library(chibi_scene_consumer OBJECT scene.c scene_prepared.c)
target_include_directories(chibi_scene_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(chibi_scene_consumer PRIVATE Chibi::scene_runtime)
target_compile_options(chibi_scene_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_executable(test_tools main.c case_timings.c case_timings_test.c graph_dispatch.c prepared.c $<TARGET_OBJECTS:chibi_record_consumer> $<TARGET_OBJECTS:chibi_prepared_consumer> $<TARGET_OBJECTS:chibi_scene_consumer> fixture.c execution_policy.c package.c chibi_cli.c native_build.c native_workloads.c third_party_publish.c collab.c
    test_policy.c validation_plan.c web_storage.c player_url.c webgpu_staging.c browser.c wasm_browser.c ../support/arena.c
    $<TARGET_OBJECTS:chibi_sdk_contracts>
    "${CHIBI_TOOL_ROOT}/src/automation/execution_policy.c"
    "${CHIBI_TOOL_ROOT}/src/host/process.c"
    "${CHIBI_TOOL_ROOT}/third_party/yyjson/yyjson.c")
target_include_directories(test_tools PRIVATE "${CHIBI_TOOL_ROOT}" "${CHIBI_TOOL_ROOT}/src"
    "${CHIBI_TOOL_ROOT}/third_party/yyjson")
target_compile_definitions(test_tools PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE
    CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}"
    CHIBI_POLICY_WORKER_PATH="$<TARGET_FILE:policy_worker>")
add_dependencies(test_tools policy_worker)
target_compile_features(test_tools PRIVATE c_std_11)
target_compile_options(test_tools PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(test_tools PRIVATE Chibi::behavior_runtime Chibi::scene_runtime Chibi::value_document)
if(TARGET Chibi::program)
    add_library(chibi_program_consumer OBJECT program_consumer.c)
    target_include_directories(chibi_program_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_program_consumer PRIVATE Chibi::program Chibi::core_capabilities)
    target_compile_options(chibi_program_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_program_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::program Chibi::core_capabilities)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_PROGRAM=1)
endif()
if(TARGET chibi_build_host)
    target_sources(test_tools PRIVATE native_aot_driver.c)
    target_sources(test_tools PRIVATE build_process.c build_toolchain.c build_graph.c build_sdk.c runtime_sdk.c)
    target_link_libraries(test_tools PRIVATE chibi_build_host)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_BUILD_PROCESS=1)
    add_dependencies(test_tools chibi-build-host)
endif()
if(TARGET chibi_compiler_host)
    target_sources(test_tools PRIVATE compiler_artifact.c compiler_bootstrap.c)
    target_link_libraries(test_tools PRIVATE chibi_compiler_host)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_COMPILER_ARTIFACT=1)
    add_dependencies(test_tools chibi-compiler-host)
endif()
if(TARGET Chibi::shader_runtime)
    add_library(chibi_shader_bake_consumer OBJECT shader_bake.c shader_source_debug.c)
    target_include_directories(chibi_shader_bake_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_shader_bake_consumer PRIVATE Chibi::shader_runtime)
    target_compile_options(chibi_shader_bake_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_shader_bake_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::shader_runtime)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SHADER_BAKE=1)
endif()
if(TARGET Chibi::authoring AND UNIX AND NOT CMAKE_CROSSCOMPILING AND NOT CHIBI_INSTALLED_SDK AND
   NOT CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" export_build_type)
    separate_arguments(export_flags NATIVE_COMMAND
        "${CMAKE_C_COMPILER_ARG1} ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${export_build_type}}")
    # Root builds supply sanitizers through directory options rather than CFLAGS.
    if(CHIBI_ENABLE_SANITIZERS AND
       (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
        CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
        list(APPEND export_flags "-fsanitize=${CHIBI_SANITIZERS}" -fno-omit-frame-pointer)
    endif()
    list(APPEND export_flags -UNDEBUG)
    set(CHIBI_EXPORT_COMPILER_ARGUMENTS "")
    foreach(argument IN ITEMS "${CMAKE_C_COMPILER}" ${export_flags})
        string(REPLACE "\\" "\\\\" argument "${argument}")
        string(REPLACE "\"" "\\\"" argument "${argument}")
        string(APPEND CHIBI_EXPORT_COMPILER_ARGUMENTS "    \"${argument}\",\n")
    endforeach()
    configure_file(export_compiler.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/export_compiler.h" @ONLY)
    set_source_files_properties(c_export.c record_c_export.c PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_sources(test_tools PRIVATE c_export.c record_c_export.c)
    target_link_libraries(test_tools PRIVATE Chibi::authoring Chibi::execution Chibi::array_capabilities ${CMAKE_DL_LIBS})
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_C_EXPORT=1)
endif()
if(TARGET Chibi::audio_runtime)
    target_sources(test_tools PRIVATE audio_analysis.c)
    target_link_libraries(test_tools PRIVATE Chibi::audio_runtime)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_AUDIO_ANALYSIS=1)
endif()
if(TARGET Chibi::scene_authoring)
    add_library(chibi_scene_authoring_consumer OBJECT ../authoring/scene_hierarchy.c)
    target_include_directories(chibi_scene_authoring_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_scene_authoring_consumer PRIVATE Chibi::scene_authoring Chibi::scene_document)
    target_compile_options(chibi_scene_authoring_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_scene_authoring_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::scene_authoring Chibi::scene_document)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SCENE_AUTHORING=1)
endif()
if(TARGET Chibi::scene_authoring AND TARGET Chibi::source_catalog AND TARGET Chibi::authoring)
    add_library(chibi_scene_snapshot_read_consumer OBJECT ../authoring/scene_snapshot_read.c)
    target_include_directories(chibi_scene_snapshot_read_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_scene_snapshot_read_consumer PRIVATE Chibi::scene_authoring Chibi::source_catalog Chibi::authoring)
    target_compile_options(chibi_scene_snapshot_read_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_scene_snapshot_read_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::scene_authoring Chibi::source_catalog Chibi::authoring)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SCENE_SNAPSHOT_READ=1)
endif()
if(TARGET Chibi::entity_runtime)
    configure_file("${CHIBI_TOOL_ROOT}/test/tools/fixture.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/tools/fixture.h" COPYONLY)
    add_library(chibi_native_consumer OBJECT native.c native_values.c native_catalog.c physics.c
        physics3d.c transforms.c tilemap_native.c terrain_seams.c terrain_checkpoint.c terrain_storage.c
        material_surface_mesh.c terrain_volume.c)
    target_include_directories(chibi_native_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_native_consumer PRIVATE Chibi::player Chibi::engine Chibi::entity_runtime Chibi::comparison_capabilities Chibi::conversion_capabilities Chibi::host_native_default
        Chibi::physics_runtime Chibi::physics3d_runtime Chibi::material_runtime Chibi::task_pool_runtime
        Chibi::spatial_runtime Chibi::tilemap_runtime Chibi::mesh_runtime)
    if(TARGET Chibi::project_compilation AND TARGET Chibi::image_preparation)
        target_link_libraries(chibi_native_consumer PRIVATE Chibi::project_compilation Chibi::image_preparation Chibi::scenario_document)
        target_link_libraries(test_tools PRIVATE Chibi::project_compilation Chibi::image_preparation Chibi::scenario_document)
        target_compile_definitions(chibi_native_consumer PRIVATE CHIBI_TEST_PREPARATION=1
            CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}")
    endif()
    target_compile_definitions(chibi_native_consumer PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
    target_compile_options(chibi_native_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_native_consumer>
        "${CHIBI_TOOL_ROOT}/src/host/native_libraries.c")
    target_link_libraries(test_tools PRIVATE Chibi::player Chibi::engine Chibi::entity_runtime Chibi::comparison_capabilities Chibi::conversion_capabilities Chibi::host_native_default
        Chibi::physics_runtime Chibi::physics3d_runtime Chibi::material_runtime Chibi::task_pool_runtime
        Chibi::spatial_runtime Chibi::tilemap_runtime Chibi::mesh_runtime)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
    target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
endif()
if(TARGET Chibi::tilemap_runtime)
    target_sources(test_tools PRIVATE tilemap.c)
    target_link_libraries(test_tools PRIVATE Chibi::tilemap_runtime)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_TILEMAP=1)
endif()
add_custom_target(check-tools COMMAND test_tools DEPENDS test_tools VERBATIM)

# Exercise the runtime GLSL compiler without opening a device. Its compiler
# header is private to Cute, so these cases require an in-tree build.
if(TARGET Chibi::platform AND TARGET Chibi::shader_runtime AND NOT CHIBI_INSTALLED_SDK)
    target_sources(test_tools PRIVATE shader_webgpu.c)
    target_include_directories(test_tools PRIVATE "${CHIBI_THIRD_PARTY_DIR}/source/cute_framework/tools")
    target_link_libraries(test_tools PRIVATE Chibi::shader_runtime Chibi::platform)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SHADER_WEBGPU=1)
endif()

# KDL asset text and the native value tree it reads into.
if(TARGET Chibi::asset_text AND TARGET Chibi::asset_json)
    add_library(chibi_asset_values_consumer OBJECT ../support/asset_text.c ../support/asset_value.c)
    target_include_directories(chibi_asset_values_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_asset_values_consumer PRIVATE Chibi::asset_text Chibi::asset_json)
    target_compile_options(chibi_asset_values_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_asset_values_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::asset_text Chibi::asset_json)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_ASSET_VALUES=1)
endif()

# The repository's published documentation site, built by a given compiler
# from copies of the pages docs/site.kdl lists.
if(TARGET Chibi::asset_text)
    target_sources(test_tools PRIVATE published_docs.c)
    target_link_libraries(test_tools PRIVATE Chibi::asset_text)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_PUBLISHED_DOCS=1)
endif()

# UI documents and RCSS, and the watched, overlaid text files they read.
if(TARGET Chibi::ui_document)
    add_library(chibi_ui_document_consumer OBJECT ../runtime/ui_document.c ../support/rcss.c
        ../support/file_watch.c ../support/text_overlay.c)
    target_include_directories(chibi_ui_document_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_ui_document_consumer PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_link_libraries(chibi_ui_document_consumer PRIVATE Chibi::ui_document)
    target_compile_options(chibi_ui_document_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_ui_document_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::ui_document)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_UI_DOCUMENT=1)
endif()

# The SDK installs no fmt header, so only an in-tree build can call it directly.
if(TARGET chibi_fmt)
    target_sources(test_tools PRIVATE ../support/fmt.c)
    target_link_libraries(test_tools PRIVATE chibi_fmt)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_FMT=1)
endif()

# Particle bursts run without a renderer.
if(TARGET Chibi::particle_runtime)
    add_library(chibi_particle_consumer OBJECT ../runtime/particles.c)
    target_include_directories(chibi_particle_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_particle_consumer PRIVATE Chibi::particle_runtime)
    target_compile_options(chibi_particle_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_particle_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::particle_runtime)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_PARTICLES=1)
endif()

# Screenshots save through Cute's PNG encoder; no window or GPU.
if(TARGET Chibi::platform)
    add_library(chibi_png_consumer OBJECT ../render/png_encoding.c)
    target_include_directories(chibi_png_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_png_consumer PRIVATE Chibi::platform)
    target_compile_options(chibi_png_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_png_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::platform)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_PNG_ENCODING=1)
endif()

# Project manifests: version gates, creation and independent fixture projects.
if(TARGET Chibi::project_document AND TARGET Chibi::text_source AND TARGET Chibi::asset_text)
    add_library(chibi_project_document_consumer OBJECT ../authoring/project_document.c)
    target_include_directories(chibi_project_document_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_project_document_consumer PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE
        CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}")
    target_link_libraries(chibi_project_document_consumer PRIVATE Chibi::project_document Chibi::text_source Chibi::asset_text)
    target_compile_options(chibi_project_document_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_project_document_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::project_document Chibi::text_source Chibi::asset_text)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_PROJECT_DOCUMENT=1)
endif()

# Reflection cases drive the native generator that SDK packages are built with.
if(CHIBI_REFLECT_EXECUTABLE)
    target_sources(test_tools PRIVATE reflect.c)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_REFLECT=1
        CHIBI_REFLECT_PATH="${CHIBI_REFLECT_EXECUTABLE}")
endif()

# The in-tree build checks the archive's Box3D directly.
if(TARGET chibi_box3d)
    target_sources(test_tools PRIVATE box3d.c)
    target_link_libraries(test_tools PRIVATE Chibi::box3d)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_BOX3D=1)
endif()

if(TARGET Chibi::render)
    target_sources(test_tools PRIVATE terrain_playable_render.c)
    add_library(chibi_render_consumer OBJECT ../authoring/render_resources.c ../render/hidden_app.c
        ../render/memory_images.c
        ../render/radiance.c ../render/radiance_depth.c ../render/material_borders.c
        ../render/material_sampling.c ../render/material_coefficients.c ../render/material_lod.c
        ../render/scene2d.c  ../render/material_terrain_seams.c
        device_controls.c ../render/interior_border.c ../render/viewport_resize.c ../draw_backend.cpp)
    target_include_directories(chibi_render_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_render_consumer PRIVATE CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}")
    target_link_libraries(chibi_render_consumer PRIVATE Chibi::render)
    target_compile_options(chibi_render_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(chibi_render_consumer PRIVATE scene3d_camera.c scene3d_terrain.c scene3d_volume.c
        terrain_cut_benchmark.c)
    target_sources(test_tools PRIVATE particle_fields.c audio_output.c player_clock.c $<TARGET_OBJECTS:chibi_render_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::render)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_RENDER_RUNTIME=1)
    configure_file("${CHIBI_TOOL_ROOT}/test/render/hidden_app.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/render/hidden_app.h" COPYONLY)
    target_sources(chibi_render_consumer PRIVATE ../render/material_ramps.c hrc_spot.c)
endif()

# Mesh surfaces and 3D shapes read the mesh parts project preparation
# publishes.
if(TARGET Chibi::render AND TARGET Chibi::project_preparation)
    target_sources(test_tools PRIVATE ../render/appearance_geometry.c ../render/mesh_surface.c
        ../render/scene3d_post.c ../render/scene3d_shapes.c)
    target_link_libraries(test_tools PRIVATE Chibi::project_preparation)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_RENDER_GEOMETRY=1)
endif()

# Image preparation decodes and imports files, so only compiler builds have it.
if(TARGET Chibi::image_preparation)
    add_library(chibi_image_preparation_consumer OBJECT ../authoring/sky_files.c)
    target_include_directories(chibi_image_preparation_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_image_preparation_consumer PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_link_libraries(chibi_image_preparation_consumer PRIVATE Chibi::image_preparation)
    target_compile_options(chibi_image_preparation_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_image_preparation_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::image_preparation)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_IMAGE_PREPARATION=1)
endif()




if(TARGET Chibi::authoring AND TARGET Chibi::host_native_default)
    configure_file("${CHIBI_TOOL_ROOT}/test/support/reflected_types.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/support/reflected_types.h" COPYONLY)
    add_library(chibi_source_runtime_consumer OBJECT
        ../runtime/whole_math.c ../runtime/whole_integer.c ../runtime/whole_conversion.c
        ../runtime/owned_tick.c ../runtime/parallel_tick.c ../runtime/whole_alias.c ../runtime/persistent_store.c

        ../support/reflected_types.c)
    target_include_directories(chibi_source_runtime_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_source_runtime_consumer PRIVATE Chibi::authoring Chibi::asset_authoring
        Chibi::behavior_runtime Chibi::arithmetic_capabilities Chibi::host_native_default
 Chibi::native_types)
    target_compile_options(chibi_source_runtime_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_source_runtime_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::authoring Chibi::asset_authoring Chibi::arithmetic_capabilities)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SOURCE_RUNTIME=1)
    target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_SOURCE_RUNTIME=1)
    target_link_libraries(chibi_sdk_contracts PRIVATE Chibi::native_types)
    if(TARGET Chibi::render AND TARGET Chibi::project_preparation)
        add_library(chibi_source_features_consumer OBJECT ../runtime/source_features.c ../runtime/source_input_save.c
             ../runtime/source_reductions.c scheduler_benchmark.c
             ../authoring/component_presentation.c ../language/corpus_semantic.c)
        target_include_directories(chibi_source_features_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
        target_link_libraries(chibi_source_features_consumer PRIVATE Chibi::project_preparation Chibi::component_presentation
            Chibi::render Chibi::host_native_default Chibi::task_pool_runtime Chibi::math_capabilities Chibi::particle_runtime Chibi::save_runtime)
        target_compile_options(chibi_source_features_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_compile_definitions(chibi_source_features_consumer PRIVATE CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}")
        target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_source_features_consumer>)
        target_link_libraries(test_tools PRIVATE Chibi::project_preparation Chibi::component_presentation Chibi::task_pool_runtime Chibi::math_capabilities Chibi::particle_runtime Chibi::save_runtime)
        target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SOURCE_FEATURES=1)
        target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_SOURCE_FEATURES=1)
        target_link_libraries(chibi_sdk_contracts PRIVATE Chibi::studio_capabilities)
        target_link_libraries(test_tools PRIVATE Chibi::studio_capabilities)
    endif()
endif()

# Material simulation cases drive internal solver headers directly, and run
# authored components through a preview world.
if(TARGET Chibi::entity_runtime AND TARGET Chibi::project_preparation AND TARGET Chibi::render)
    target_sources(test_tools PRIVATE ../runtime/materials.c ../runtime/material_fixture.c

        ../runtime/material_fluids.c
         ../runtime/material_debris.c ../runtime/reference_cave.c

         ../runtime/material_mixed.c ../runtime/material_mpm.c
        ../runtime/material_obstacles.c ../runtime/material_particle_index.c
         ../runtime/material_simulation.c
        ../runtime/material_benchmark.c )
    target_link_libraries(test_tools PRIVATE Chibi::project_preparation Chibi::render)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_MATERIAL_RUNTIME=1)
endif()

# Authoring documents over the game's reflected components and graph nodes:
# prototype and scene documents, the reference index and asset source edits.
if(TARGET chibi_source_runtime_consumer )
    target_sources(test_tools PRIVATE ../authoring/prototype_documents.c)
    target_link_libraries(test_tools PRIVATE Chibi::reference_index
        Chibi::prototype_document Chibi::scene_document Chibi::native_types)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_AUTHORING_DOCUMENTS=1)
endif()

add_executable(benchmark_trials "${CHIBI_TOOL_ROOT}/tools/benchmark.c"
    "${CHIBI_TOOL_ROOT}/src/host/process.c")
target_include_directories(benchmark_trials PRIVATE "${CHIBI_TOOL_ROOT}/src")
target_compile_definitions(benchmark_trials PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_features(benchmark_trials PRIVATE c_std_11)
target_compile_options(benchmark_trials PRIVATE -Wall -Wextra -Werror)
add_dependencies(test_tools benchmark_trials)
