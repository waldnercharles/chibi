# Synthetic inputs for configure/install metadata checks. The cached-package
# entrypoint performs ordinary compiler detection, but builds no dependency.
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_DL_LIBS "dl")

function(fixture_inputs root)
    file(MAKE_DIRECTORY "${root}/lib" "${root}/include/sdk"
        "${root}/include/crypto/openssl" "${root}/include/zlib"
        "${root}/other/include/openssl" "${root}/opengl" "${root}/unowned")
    foreach(archive fixture crypto z)
        file(WRITE "${root}/lib/lib${archive}.a" "!<arch>\n")
    endforeach()
    file(WRITE "${root}/include/sdk/fixture.h" "#define FIXTURE_SDK 1\n")
    file(WRITE "${root}/include/crypto/openssl/crypto.h" "#define FIXTURE_CRYPTO 1\n")
    file(WRITE "${root}/include/zlib/zlib.h" "#define FIXTURE_ZLIB 1\n")
    file(WRITE "${root}/other/include/openssl/crypto.h" "#define FIXTURE_OTHER_CRYPTO 1\n")
    file(WRITE "${root}/other/libcrypto.a" "!<arch>\n")
    foreach(role GL OpenGL GLX)
        file(WRITE "${root}/opengl/lib${role}.so" "fixture ${role}\n")
    endforeach()
    file(WRITE "${root}/unowned/libGL.so" "unowned GL\n")
endfunction()

function(fixture_property target property expected)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "fixture: missing target ${target}")
    endif()
    get_target_property(actual "${target}" "${property}")
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "fixture: ${target} ${property}: '${actual}', expected '${expected}'")
    endif()
endfunction()
