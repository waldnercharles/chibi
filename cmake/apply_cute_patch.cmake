if(NOT DEFINED CUTE_SOURCE OR NOT DEFINED CUTE_PATCH)
    message(FATAL_ERROR "CUTE_SOURCE and CUTE_PATCH are required")
endif()
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CUTE_SOURCE}" apply --reverse --check "${CUTE_PATCH}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(already_applied EQUAL 0)
    return()
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CUTE_SOURCE}" apply --check "${CUTE_PATCH}"
    RESULT_VARIABLE check_result
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "Cute renderer patch does not match the dependency checkout: ${check_error}")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CUTE_SOURCE}" apply "${CUTE_PATCH}"
    RESULT_VARIABLE apply_result
    ERROR_VARIABLE apply_error
)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "Could not apply Cute renderer patch: ${apply_error}")
endif()
