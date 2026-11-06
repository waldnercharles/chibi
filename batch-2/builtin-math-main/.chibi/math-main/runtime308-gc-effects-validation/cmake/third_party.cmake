# Loads the prebuilt third-party archive for this build's target and variant.
# third_party/archives.tsv pins each archive by SHA-256; an archive unpacks
# into CHIBI_THIRD_PARTY_CACHE/<sha256>, .git/chibi/third_party in the clone's
# Git directory, which its worktrees share. tools/toolchain/Build.chibi
# downloads the archives before configuring. A configuration that starts from
# CMake alone (recovery, web, tool builds) downloads a missing archive here.
# Temporary: this goes with CMake.
include_guard(GLOBAL)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(third_party_target web)
    if(CHIBI_WEB_THREADS)
        set(third_party_target web-threads)
    elseif(CHIBI_WEB_PLAYER)
        set(third_party_target web-player)
    endif()
else()
    if(APPLE)
        set(third_party_target macos)
    elseif(WIN32)
        set(third_party_target windows)
    else()
        set(third_party_target linux)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
        string(APPEND third_party_target -arm64)
    else()
        string(APPEND third_party_target -x64)
    endif()
endif()
# Sanitized builds link libraries instrumented the same way; others link release.
# The instrumented archives are built with upstream LLVM, whose sanitizer
# runtime Apple clang's does not provide, so Apple clang links release.
set(third_party_variant release)
if(CHIBI_ENABLE_SANITIZERS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND
   NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    if(CHIBI_SANITIZERS STREQUAL "address,undefined")
        set(third_party_variant address-undefined)
    elseif(CHIBI_SANITIZERS STREQUAL "thread")
        set(third_party_variant thread)
    endif()
endif()

set(third_party_manifest "${CMAKE_CURRENT_LIST_DIR}/../third_party/archives.tsv")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${third_party_manifest}")
file(STRINGS "${third_party_manifest}" third_party_lines)
set(third_party_repository "")
set(third_party_release "")
set(third_party_sha256 "")
foreach(line IN LISTS third_party_lines)
    if(line MATCHES "^# repository ([^ ]+)$")
        set(third_party_repository "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^${third_party_target}\t${third_party_variant}\t([^\t]+)\t([0-9a-f]+)$")
        set(third_party_release "${CMAKE_MATCH_1}")
        set(third_party_sha256 "${CMAKE_MATCH_2}")
    endif()
endforeach()
if(NOT third_party_sha256)
    message(FATAL_ERROR "third_party/archives.tsv has no ${third_party_target} ${third_party_variant} archive")
endif()

if(NOT CHIBI_THIRD_PARTY_CACHE)
    get_filename_component(third_party_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    find_package(Git QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${third_party_root}" rev-parse
            --path-format=absolute --git-common-dir
        OUTPUT_VARIABLE git_directory OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result ERROR_QUIET)
    if(GIT_FOUND AND result EQUAL 0)
        set(CHIBI_THIRD_PARTY_CACHE "${git_directory}/chibi/third_party")
    else()
        set(CHIBI_THIRD_PARTY_CACHE "${third_party_root}/.chibi/third_party")
    endif()
endif()
set(CHIBI_THIRD_PARTY_DIR "${CHIBI_THIRD_PARTY_CACHE}/${third_party_sha256}"
    CACHE INTERNAL "Unpacked third-party archive")
if(NOT EXISTS "${CHIBI_THIRD_PARTY_DIR}/third-party.cmake")
    set(asset "third-party-${third_party_target}-${third_party_variant}.tar.gz")
    find_program(CHIBI_GH gh)
    if(NOT CHIBI_GH)
        message(FATAL_ERROR "The ${asset} third-party archive is not in ${CHIBI_THIRD_PARTY_CACHE}. "
            "Install and authenticate the GitHub CLI (gh) to download it.")
    endif()
    # Named for this build directory, so an interrupted download is replaced by
    # the next configure here instead of left behind, and never shared.
    string(SHA256 suffix "${CMAKE_BINARY_DIR}")
    set(partial "${CHIBI_THIRD_PARTY_CACHE}/${third_party_sha256}.${suffix}.partial")
    file(REMOVE_RECURSE "${partial}")
    file(MAKE_DIRECTORY "${partial}")
    message(STATUS "Downloading ${asset} from ${third_party_repository} ${third_party_release}")
    execute_process(COMMAND "${CHIBI_GH}" release download "${third_party_release}"
            --repo "${third_party_repository}" --pattern "${asset}" --dir "${partial}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        file(REMOVE_RECURSE "${partial}")
        message(FATAL_ERROR "Could not download ${asset} from ${third_party_repository} ${third_party_release}")
    endif()
    file(SHA256 "${partial}/${asset}" actual)
    if(NOT actual STREQUAL third_party_sha256)
        file(REMOVE_RECURSE "${partial}")
        message(FATAL_ERROR "${asset} has SHA-256 ${actual}; third_party/archives.tsv pins ${third_party_sha256}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${partial}/${asset}" DESTINATION "${partial}/tree")
    # Another configuration may have published the same archive first; rename
    # refuses to replace its tree, which is never empty. CMake 4.4's NO_REPLACE
    # fails for every directory on macOS.
    file(RENAME "${partial}/tree" "${CHIBI_THIRD_PARTY_DIR}" RESULT renamed)
    file(REMOVE_RECURSE "${partial}")
    if(NOT EXISTS "${CHIBI_THIRD_PARTY_DIR}/third-party.cmake")
        message(FATAL_ERROR "Could not unpack ${asset} into ${CHIBI_THIRD_PARTY_DIR}: ${renamed}")
    endif()
endif()
include("${CHIBI_THIRD_PARTY_DIR}/third-party.cmake")
