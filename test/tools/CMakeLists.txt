cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(chibi_tooling C)
endif()

set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "")
get_filename_component(CHIBI_TOOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
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
    set(CHIBI_TEST_ADOPT_DEVICE 1)
    if(CHIBI_TEST_WEBGPU_BASELINE)
        set(CHIBI_TEST_ADOPT_DEVICE 0)
    endif()
    configure_file("${CHIBI_TOOL_ROOT}/test/render/webgpu_staging.html"
        "${CMAKE_CURRENT_BINARY_DIR}/webgpu_staging.html" @ONLY)
    add_executable(test_tools main.c ../render/webgpu_staging.c ../draw_backend.cpp)
    target_include_directories(test_tools PRIVATE "${CHIBI_TOOL_ROOT}" "${CHIBI_TOOL_ROOT}/src")
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_WEBGPU_STAGING=1
        CHIBI_TEST_ADOPT_DEVICE=${CHIBI_TEST_ADOPT_DEVICE})
    target_compile_features(test_tools PRIVATE c_std_11)
    target_compile_options(test_tools PRIVATE -Wall -Wextra -Werror -Wno-experimental -UNDEBUG)
    target_link_libraries(test_tools PRIVATE ${CHIBI_TEST_PLATFORM})
    set_target_properties(test_tools PROPERTIES SUFFIX ".html" LINKER_LANGUAGE CXX
        LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/webgpu_staging.html")
    target_link_options(test_tools PRIVATE
        "--shell-file=${CMAKE_CURRENT_BINARY_DIR}/webgpu_staging.html"
        "-sALLOW_MEMORY_GROWTH=1" "-sSTACK_SIZE=1048576")
    if(CHIBI_TEST_WEBGPU_BASELINE)
        target_link_options(test_tools PRIVATE "-sASYNCIFY=1")
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
target_include_directories(chibi_prepared_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(chibi_prepared_consumer PRIVATE Chibi::behavior_runtime)
target_compile_options(chibi_prepared_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_library(chibi_scene_consumer OBJECT scene.c scene_prepared.c)
target_include_directories(chibi_scene_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(chibi_scene_consumer PRIVATE Chibi::scene_runtime)
target_compile_options(chibi_scene_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_executable(test_tools main.c prepared.c $<TARGET_OBJECTS:chibi_prepared_consumer> $<TARGET_OBJECTS:chibi_scene_consumer> fixture.c execution_policy.c package.c collab.c
    validate_project.c test_policy.c validation_plan.c publish.c web_storage.c webgpu_staging.c $<TARGET_OBJECTS:chibi_sdk_contracts>
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
if(TARGET Chibi::entity_runtime)
    add_library(chibi_native_consumer OBJECT native.c native_values.c native_catalog.c physics.c
        physics3d.c transforms.c tilemap_native.c)
    target_include_directories(chibi_native_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_native_consumer PRIVATE Chibi::player Chibi::engine Chibi::entity_runtime Chibi::comparison_capabilities Chibi::conversion_capabilities Chibi::host_native_default
        Chibi::physics_runtime Chibi::physics3d_runtime Chibi::material_runtime Chibi::task_pool_runtime
        Chibi::spatial_runtime Chibi::tilemap_runtime Chibi::mesh_runtime)
    if(TARGET Chibi::project_compilation AND TARGET Chibi::image_preparation)
        target_link_libraries(chibi_native_consumer PRIVATE Chibi::project_compilation Chibi::image_preparation)
        target_link_libraries(test_tools PRIVATE Chibi::project_compilation Chibi::image_preparation)
        target_compile_definitions(chibi_native_consumer PRIVATE CHIBI_TEST_PREPARATION=1
            CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}")
    endif()
    target_compile_definitions(chibi_native_consumer PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
    target_compile_options(chibi_native_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_native_consumer>)
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
if(TARGET Chibi::tilemap_runtime AND TARGET Chibi::asset_text AND TARGET Chibi::project_compilation)
    target_sources(test_tools PRIVATE tilemap_project.c)
    target_link_libraries(test_tools PRIVATE Chibi::asset_text)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_TILEMAP_PROJECT=1)
endif()
add_custom_target(check-tools COMMAND test_tools DEPENDS test_tools VERBATIM)

# Reflection cases drive the native generator that SDK packages are built with.
if(CHIBI_REFLECT_EXECUTABLE)
    target_sources(test_tools PRIVATE reflect.c)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_REFLECT=1
        CHIBI_REFLECT_PATH="${CHIBI_REFLECT_EXECUTABLE}")
endif()

# The SDK installs no Box3D headers, so only an in-tree build can call Box3D directly.
if(TARGET chibi_box3d)
    target_sources(test_tools PRIVATE box3d.c)
    target_link_libraries(test_tools PRIVATE Chibi::box3d)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_BOX3D=1)
endif()

if(TARGET Chibi::render)
    add_library(chibi_render_consumer OBJECT ../authoring/render_resources.c ../render/hidden_app.c
        ../render/memory_images.c
        ../render/radiance.c ../render/radiance_depth.c ../render/material_borders.c
        ../render/material_sampling.c ../render/scene2d.c ../render/scene3d.c
        device_controls.c ../render/interior_border.c ../render/viewport_resize.c ../draw_backend.cpp)
    target_include_directories(chibi_render_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_render_consumer PRIVATE CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}")
    target_link_libraries(chibi_render_consumer PRIVATE Chibi::render)
    target_compile_options(chibi_render_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_render_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::render)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_RENDER_RUNTIME=1)
    # Surface materials with roughness and metallic come from material graphs, and
    # the visibility scenes build shader graphs too.
    if(TARGET Chibi::shader_preparation)
        target_link_libraries(chibi_render_consumer PRIVATE Chibi::shader_preparation
            Chibi::graph_compilation Chibi::graph_authoring)
        target_sources(chibi_render_consumer PRIVATE ../render/material_graphs.c ../render/material_ramps.c
            ../render/material_pixels.c ../sdf_visibility.c)
        target_compile_definitions(chibi_render_consumer PRIVATE CHIBI_TEST_MATERIAL_GRAPHS=1)
        target_link_libraries(test_tools PRIVATE Chibi::shader_preparation
            Chibi::graph_compilation Chibi::graph_authoring)
        target_compile_definitions(test_tools PRIVATE CHIBI_TEST_MATERIAL_GRAPHS=1)
        target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_MATERIAL_GRAPHS=1)
    endif()
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

# Mesh preparation reads `.mesh` content files, and model export writes them,
# so only compiler builds have them.
if(TARGET Chibi::model_authoring)
    add_library(chibi_mesh_preparation_consumer OBJECT ../authoring/mesh_files.c)
    target_include_directories(chibi_mesh_preparation_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_mesh_preparation_consumer PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_link_libraries(chibi_mesh_preparation_consumer PRIVATE Chibi::model_authoring Chibi::shader_preparation
        Chibi::mesh_runtime Chibi::graph_authoring Chibi::graph_compilation Chibi::graph_model)
    target_compile_options(chibi_mesh_preparation_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_mesh_preparation_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::model_authoring Chibi::shader_preparation
        Chibi::mesh_runtime Chibi::graph_authoring Chibi::graph_compilation Chibi::graph_model)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_MESH_PREPARATION=1)
endif()

# Model graphs compile to distance programs, bounds and hit resolvers.
if(TARGET Chibi::model_authoring)
    target_sources(test_tools PRIVATE ../graph/packs.c ../graph/modeling.c ../graph/model_constructive.c
        ../graph/model_profiles.c ../graph/model_reuse.c)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_MODEL_GRAPHS=1)
    target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_MODEL_GRAPHS=1)
endif()

# Model import reads glTF files and writes content files, so only compiler
# builds have it. The render cases draw what it writes.
if(TARGET Chibi::model_import)
    configure_file("${CHIBI_TOOL_ROOT}/test/support/gltf_fixture.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/support/gltf_fixture.h" COPYONLY)
    add_library(chibi_model_import_consumer OBJECT ../authoring/model_import.c ../support/gltf_fixture.c)
    target_include_directories(chibi_model_import_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_compile_definitions(chibi_model_import_consumer PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_link_libraries(chibi_model_import_consumer PRIVATE Chibi::model_import Chibi::mesh_runtime
        Chibi::graph_authoring Chibi::graph_compilation Chibi::native_types Chibi::asset_text Chibi::platform)
    target_compile_options(chibi_model_import_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_model_import_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::model_import Chibi::mesh_runtime Chibi::graph_authoring
        Chibi::graph_compilation Chibi::native_types Chibi::asset_text Chibi::platform)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_MODEL_IMPORT=1)
    target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_MODEL_IMPORT=1)
    if(TARGET chibi_render_consumer)
        target_link_libraries(chibi_render_consumer PRIVATE Chibi::model_import Chibi::image_preparation)
        target_compile_definitions(chibi_render_consumer PRIVATE CHIBI_TEST_MODEL_IMPORT=1)
    endif()
endif()

if(TARGET Chibi::authoring AND TARGET Chibi::host_native_default)
    configure_file("${CHIBI_TOOL_ROOT}/test/support/reflected_types.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/support/reflected_types.h" COPYONLY)
    add_library(chibi_source_runtime_consumer OBJECT
        ../runtime/whole_math.c ../runtime/whole_integer.c ../runtime/whole_conversion.c
        ../runtime/owned_tick.c ../runtime/parallel_tick.c ../runtime/whole_alias.c ../runtime/persistent_store.c
        ../authoring/new_asset.c ../authoring/asset_refactor.c
        ../support/reflected_types.c)
    target_include_directories(chibi_source_runtime_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_source_runtime_consumer PRIVATE Chibi::authoring Chibi::asset_authoring
        Chibi::behavior_runtime Chibi::arithmetic_capabilities Chibi::host_native_default)
    target_compile_options(chibi_source_runtime_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_source_runtime_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::authoring Chibi::asset_authoring Chibi::arithmetic_capabilities)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SOURCE_RUNTIME=1)
    target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_SOURCE_RUNTIME=1)
    target_link_libraries(chibi_sdk_contracts PRIVATE Chibi::native_types)
    if(TARGET Chibi::render AND TARGET Chibi::project_preparation)
        add_library(chibi_source_features_consumer OBJECT ../runtime/source_features.c ../authoring/source_presentation.c
            ../runtime/system_schedule.c ../code_system_runtime.c ../authoring/source_edit.c
            ../authoring/shader_parameters.c ../authoring/component_presentation.c ../language/corpus_semantic.c)
        target_include_directories(chibi_source_features_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
        target_link_libraries(chibi_source_features_consumer PRIVATE Chibi::project_preparation Chibi::component_presentation
            Chibi::render Chibi::graph_catalog Chibi::host_native_default Chibi::task_pool_runtime Chibi::math_capabilities Chibi::particle_runtime)
        target_compile_options(chibi_source_features_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_source_features_consumer>)
        target_link_libraries(test_tools PRIVATE Chibi::project_preparation Chibi::component_presentation Chibi::graph_catalog Chibi::task_pool_runtime Chibi::math_capabilities Chibi::particle_runtime)
        target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SOURCE_FEATURES=1)
        target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_SOURCE_FEATURES=1)
        target_link_libraries(chibi_sdk_contracts PRIVATE Chibi::studio_capabilities)
        target_link_libraries(test_tools PRIVATE Chibi::studio_capabilities)
        # Feature coverage runs each fixture scene through chibi_ctl's run_scenario.
        if(TARGET chibi_ctl)
            target_sources(test_tools PRIVATE feature_coverage.c)
            target_compile_definitions(test_tools PRIVATE CHIBI_TEST_FEATURE_COVERAGE=1
                CHIBI_CTL_PATH="$<TARGET_FILE:chibi_ctl>")
            add_dependencies(test_tools chibi_ctl)
        endif()
    endif()
endif()

add_executable(benchmark_trials "${CHIBI_TOOL_ROOT}/tools/benchmark.c"
    "${CHIBI_TOOL_ROOT}/src/host/process.c")
target_include_directories(benchmark_trials PRIVATE "${CHIBI_TOOL_ROOT}/src")
target_compile_definitions(benchmark_trials PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_features(benchmark_trials PRIVATE c_std_11)
target_compile_options(benchmark_trials PRIVATE -Wall -Wextra -Werror)
