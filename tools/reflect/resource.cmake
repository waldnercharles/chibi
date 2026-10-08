# Resolve builtin headers from the selected libclang installation, rather than
# the compiler driving this build (which may be a different Clang or GCC).
function(chibi_reflect_resource_include output)
    get_filename_component(library "${LIBCLANG_LIBRARY}" REALPATH)
    # Keep the found path's directory: distributions symlink it out of the LLVM prefix.
    get_filename_component(library_dir "${LIBCLANG_LIBRARY}" DIRECTORY)
    get_filename_component(prefix "${library_dir}/.." REALPATH)
    set(clang "${prefix}/bin/clang")
    if(EXISTS "${clang}")
        execute_process(COMMAND "${clang}" -print-resource-dir
            RESULT_VARIABLE result OUTPUT_VARIABLE resource
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(result EQUAL 0 AND EXISTS "${resource}/include/stdbool.h")
            set(${output} "${resource}/include" PARENT_SCOPE)
            return()
        endif()
    endif()
    # Some distributions ship libclang and its headers without the driver.
    file(GLOB resources "${library_dir}/clang/*/include")
    list(FILTER resources INCLUDE REGEX "/include$")
    if(library MATCHES "libclang[-.]([0-9]+)")
        set(major "${CMAKE_MATCH_1}")
        list(FILTER resources INCLUDE REGEX "/clang/${major}([./]|$)")
    endif()
    list(LENGTH resources count)
    if(count EQUAL 1)
        list(GET resources 0 resource)
        if(EXISTS "${resource}/stdbool.h")
            set(${output} "${resource}" PARENT_SCOPE)
            return()
        endif()
    endif()
    message(FATAL_ERROR "Cannot find builtin headers matching ${LIBCLANG_LIBRARY}; install that LLVM resource directory")
endfunction()
