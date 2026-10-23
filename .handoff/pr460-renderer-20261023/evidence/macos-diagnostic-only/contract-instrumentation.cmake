if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "/Users/cwaldner/Personal/chibi/.worktrees/recover-pr460/chibi/third_party")
    cmake_language(DEFER CALL set_source_files_properties
        "/tmp/chibi-integration-20261022/pr460/dependency-sources/cute_framework/src/cute_draw.cpp"
        TARGET_DIRECTORY cute
        PROPERTIES COMPILE_OPTIONS
        "-include;/tmp/chibi-integration-20261022/pr460/memcmp_nonnull.h")
endif()
