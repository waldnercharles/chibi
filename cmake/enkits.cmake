# The SDK already owns and installs its task scheduler implementation.
if(TARGET chibi_task_scheduler)
    return()
endif()

# Native and isolated threaded web builds share the same task executor.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_THREADS)
    set(ENKITS_BUILD_C_INTERFACE ON CACHE BOOL "" FORCE)
    set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENKITS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)
    # Upstream's switch hardcodes ASan+UBSan. Apply our selected sanitizer to
    # this target below so a ThreadSanitizer build also instruments its workers.
    set(ENKITS_SANITIZE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(enkits
        GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
        GIT_TAG 4cba61a045d8e759bd35e5d85691bf9f7271f822
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(enkits)
endif()
