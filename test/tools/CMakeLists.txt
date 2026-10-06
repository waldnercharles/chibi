cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(chibi_tooling C)
endif()

get_filename_component(CHIBI_TOOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(policy_worker policy_worker.c)
target_compile_definitions(policy_worker PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
target_compile_features(policy_worker PRIVATE c_std_11)
target_compile_options(policy_worker PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_executable(test_tools main.c fixture.c execution_policy.c package.c collab.c studio_selectors.c
    test_policy.c validation_plan.c
    "${CHIBI_TOOL_ROOT}/src/automation/execution_policy.c"
    "${CHIBI_TOOL_ROOT}/src/host/process.c"
    "${CHIBI_TOOL_ROOT}/src/support/xalloc.c"
    "${CHIBI_TOOL_ROOT}/third_party/yyjson/yyjson.c")
target_include_directories(test_tools PRIVATE "${CHIBI_TOOL_ROOT}" "${CHIBI_TOOL_ROOT}/src"
    "${CHIBI_TOOL_ROOT}/third_party/yyjson")
target_compile_definitions(test_tools PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE
    CHIBI_SOURCE_ROOT="${CHIBI_TOOL_ROOT}"
    CHIBI_POLICY_WORKER_PATH="$<TARGET_FILE:policy_worker>")
add_dependencies(test_tools policy_worker)
target_compile_features(test_tools PRIVATE c_std_11)
target_compile_options(test_tools PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_custom_target(check-tools COMMAND test_tools DEPENDS test_tools VERBATIM)

add_executable(benchmark_trials "${CHIBI_TOOL_ROOT}/tools/benchmark.c"
    "${CHIBI_TOOL_ROOT}/src/host/process.c")
target_include_directories(benchmark_trials PRIVATE "${CHIBI_TOOL_ROOT}/src")
target_compile_definitions(benchmark_trials PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_features(benchmark_trials PRIVATE c_std_11)
target_compile_options(benchmark_trials PRIVATE -Wall -Wextra -Werror)
