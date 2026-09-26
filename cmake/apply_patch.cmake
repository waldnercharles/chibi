# Applies PATCH_FILE to the dependency checkout PATCH_SOURCE once; an applied patch is left alone.
if(NOT DEFINED PATCH_SOURCE OR NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "PATCH_SOURCE and PATCH_FILE are required")
endif()
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PATCH_SOURCE}" apply --reverse --check "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(already_applied EQUAL 0)
    return()
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PATCH_SOURCE}" apply --check "${PATCH_FILE}"
    RESULT_VARIABLE check_result
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "${PATCH_FILE} does not match the dependency checkout: ${check_error}")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PATCH_SOURCE}" apply "${PATCH_FILE}"
    RESULT_VARIABLE apply_result
    ERROR_VARIABLE apply_error
)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "Could not apply ${PATCH_FILE}: ${apply_error}")
endif()
