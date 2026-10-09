cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(chibi_tooling C)
endif()

set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "")
get_filename_component(CHIBI_TOOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
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
add_library(chibi_sdk_contracts OBJECT types.c ecs.c)
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

add_executable(test_tools main.c prepared.c $<TARGET_OBJECTS:chibi_prepared_consumer> $<TARGET_OBJECTS:chibi_scene_consumer> fixture.c execution_policy.c package.c collab.c studio_selectors.c
    test_policy.c validation_plan.c publish.c $<TARGET_OBJECTS:chibi_sdk_contracts>
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
    add_library(chibi_native_consumer OBJECT native.c native_values.c native_catalog.c)
    target_include_directories(chibi_native_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_native_consumer PRIVATE Chibi::player Chibi::engine Chibi::entity_runtime Chibi::comparison_capabilities Chibi::conversion_capabilities Chibi::host_native_default)
    target_compile_definitions(chibi_native_consumer PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
    target_compile_options(chibi_native_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_native_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::player Chibi::engine Chibi::entity_runtime Chibi::comparison_capabilities Chibi::conversion_capabilities Chibi::host_native_default)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
    target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_NATIVE_RUNTIME=1)
endif()
add_custom_target(check-tools COMMAND test_tools DEPENDS test_tools VERBATIM)

if(TARGET Chibi::render)
    add_library(chibi_render_consumer OBJECT ../authoring/render_resources.c)
    target_include_directories(chibi_render_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_render_consumer PRIVATE Chibi::render)
    target_compile_options(chibi_render_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_render_consumer>)
    target_link_libraries(test_tools PRIVATE Chibi::render)
    target_compile_definitions(test_tools PRIVATE CHIBI_TEST_RENDER_RUNTIME=1)
endif()

if(TARGET Chibi::authoring AND TARGET Chibi::host_native_default)
    configure_file("${CHIBI_TOOL_ROOT}/test/support/reflected_types.h"
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/support/reflected_types.h" COPYONLY)
    add_library(chibi_source_runtime_consumer OBJECT
        ../runtime/whole_math.c ../runtime/whole_integer.c ../runtime/whole_conversion.c
        ../runtime/owned_tick.c ../runtime/parallel_tick.c ../runtime/whole_alias.c ../runtime/persistent_store.c
        ../authoring/new_asset.c
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
            ../authoring/shader_parameters.c ../language/corpus_semantic.c)
        target_include_directories(chibi_source_features_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
        target_link_libraries(chibi_source_features_consumer PRIVATE Chibi::project_preparation
            Chibi::render Chibi::graph_catalog Chibi::host_native_default Chibi::task_pool_runtime Chibi::math_capabilities Chibi::particle_runtime)
        target_compile_options(chibi_source_features_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        target_sources(test_tools PRIVATE $<TARGET_OBJECTS:chibi_source_features_consumer>)
        target_link_libraries(test_tools PRIVATE Chibi::project_preparation Chibi::graph_catalog Chibi::task_pool_runtime Chibi::math_capabilities Chibi::particle_runtime)
        target_compile_definitions(test_tools PRIVATE CHIBI_TEST_SOURCE_FEATURES=1)
        target_compile_definitions(chibi_sdk_contracts PRIVATE CHIBI_TEST_SOURCE_FEATURES=1)
        target_link_libraries(chibi_sdk_contracts PRIVATE Chibi::studio_capabilities)
        target_link_libraries(test_tools PRIVATE Chibi::studio_capabilities)
    endif()
endif()

add_executable(benchmark_trials "${CHIBI_TOOL_ROOT}/tools/benchmark.c"
    "${CHIBI_TOOL_ROOT}/src/host/process.c")
target_include_directories(benchmark_trials PRIVATE "${CHIBI_TOOL_ROOT}/src")
target_compile_definitions(benchmark_trials PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_features(benchmark_trials PRIVATE c_std_11)
target_compile_options(benchmark_trials PRIVATE -Wall -Wextra -Werror)
