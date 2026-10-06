# Keep s2n's provider selection, including supplied targets and shared fallback.
if(TARGET crypto OR TARGET AWS::crypto)
    include("${s2n_SOURCE_DIR}/cmake/modules/Findcrypto.cmake")
    return()
endif()
include("${s2n_SOURCE_DIR}/cmake/modules/Findcrypto.cmake")
if(NOT TARGET AWS::crypto OR NOT crypto_LIBRARY MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$")
    return()
endif()

# The legacy cloud activation predates default-provenance markers. Recognize
# only its exact native search defaults and independently verified host provider.
function(chibi_cloud_crypto_metadata result)
    set(${result} FALSE PARENT_SCOPE)
    set(cloud "$ENV{CHIBI_CLOUD_ACTIVE}")
    if(NOT cloud OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING OR
       NOT CMAKE_LIBRARY_ARCHITECTURE STREQUAL "x86_64-linux-gnu" OR
       DEFINED CMAKE_TOOLCHAIN_FILE OR DEFINED ENV{CMAKE_TOOLCHAIN_FILE} OR
       DEFINED CMAKE_SYSROOT OR DEFINED CMAKE_SYSROOT_COMPILE OR DEFINED CMAKE_SYSROOT_LINK OR
       DEFINED CMAKE_FIND_ROOT_PATH OR CMAKE_C_COMPILER_TARGET OR
       DEFINED CMAKE_PREFIX_PATH OR DEFINED OPENSSL_ROOT_DIR OR DEFINED ENV{OPENSSL_ROOT_DIR} OR
       DEFINED CACHE{crypto_LIBRARY} OR DEFINED CACHE{CMAKE_MODULE_PATH} OR
       DEFINED ENV{PKG_CONFIG_LIBDIR} OR DEFINED ENV{PKG_CONFIG} OR PKG_CONFIG_ARGN OR
       TARGET OpenSSL::Crypto)
        return()
    endif()
    if(NOT "$ENV{PKG_CONFIG_SYSROOT_DIR}" STREQUAL "${cloud}/root" OR
       NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "${cloud}/root/usr" OR
       NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "${cloud}/root/usr/lib/x86_64-linux-gnu/pkgconfig:${cloud}/root/usr/share/pkgconfig" OR
       NOT EXISTS "${cloud}/root/usr/bin/clang-19" OR NOT EXISTS "${cloud}/python/bin/cmake" OR
       EXISTS "${cloud}/root/usr/lib/x86_64-linux-gnu/libcrypto.a" OR
       EXISTS "${cloud}/root/usr/include/openssl/crypto.h")
        return()
    endif()
    file(REAL_PATH "${crypto_LIBRARY}" selected_library)
    file(REAL_PATH "${crypto_INCLUDE_DIR}" selected_include)
    file(REAL_PATH "/usr/lib/x86_64-linux-gnu/libcrypto.a" host_library)
    file(REAL_PATH "/usr/include" host_include)
    if(NOT selected_library STREQUAL host_library OR NOT selected_include STREQUAL host_include)
        return()
    endif()
    find_package(PkgConfig QUIET)
    if(NOT PkgConfig_FOUND OR PKG_CONFIG_ARGN)
        return()
    endif()
    file(REAL_PATH "${PKG_CONFIG_EXECUTABLE}" pkg_config)
    file(REAL_PATH "/usr/bin/pkg-config" host_pkg_config)
    if(NOT pkg_config STREQUAL host_pkg_config)
        return()
    endif()
    # pkgconf --path reports actual files even when --variable=pcfiledir is remapped.
    execute_process(COMMAND "${PKG_CONFIG_EXECUTABLE}" --path openssl libcrypto
        RESULT_VARIABLE status OUTPUT_VARIABLE origins OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(status EQUAL 0 AND origins STREQUAL "/usr/lib/x86_64-linux-gnu/pkgconfig/openssl.pc\n/usr/lib/x86_64-linux-gnu/pkgconfig/libcrypto.pc")
        set(${result} TRUE PARENT_SCOPE)
    endif()
endfunction()

# s2n 1.6.3 omits static libcrypto's private dependencies. Use CMake's
# OpenSSL metadata only for the same library and headers; never switch providers.
function(chibi_crypto_dependencies)
    # FindOpenSSL temporarily restricts library suffixes to archives. Discover
    # optional zlib first with the caller's normal discovery policy.
    find_package(ZLIB QUIET)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    chibi_cloud_crypto_metadata(host_metadata)
    if(host_metadata)
        message(STATUS "Using verified host OpenSSL metadata for the native cloud extraction")
        set(saved_sysroot "$ENV{PKG_CONFIG_SYSROOT_DIR}")
        unset(ENV{PKG_CONFIG_SYSROOT_DIR})
        # FindPkgConfig does not key this cache on its environment. Refresh only
        # OpenSSL, including a build directory previously configured with remapping.
        set(_OPENSSL_FOUND FALSE)
    endif()
    find_package(OpenSSL QUIET COMPONENTS Crypto)
    if(_OPENSSL_FOUND)
        pkg_get_variable(CHIBI_CRYPTO_LIBDIR libcrypto libdir)
        pkg_get_variable(CHIBI_CRYPTO_INCLUDEDIR libcrypto includedir)
    endif()
    if(host_metadata)
        set(ENV{PKG_CONFIG_SYSROOT_DIR} "${saved_sysroot}")
    endif()
    if(NOT OpenSSL_FOUND OR NOT TARGET OpenSSL::Crypto)
        message(FATAL_ERROR "OpenSSL Crypto discovery failed for ${crypto_LIBRARY} and ${crypto_INCLUDE_DIR}.")
    endif()
    get_target_property(library OpenSSL::Crypto IMPORTED_LOCATION)
    get_target_property(includes OpenSSL::Crypto INTERFACE_INCLUDE_DIRECTORIES)
    file(REAL_PATH "${library}" openssl_library)
    file(REAL_PATH "${crypto_LIBRARY}" selected_library)
    file(REAL_PATH "${includes}" openssl_include)
    file(REAL_PATH "${crypto_INCLUDE_DIR}" selected_include)
    if(NOT openssl_library STREQUAL selected_library OR NOT openssl_include STREQUAL selected_include)
        message(FATAL_ERROR "OpenSSL metadata does not match s2n's selected crypto library/headers. "
            "Supply a crypto target with its link dependencies, or align OpenSSL discovery "
            "with crypto_LIBRARY and crypto_INCLUDE_DIR.")
    endif()
    # FindOpenSSL can retain explicit library/header paths while reading another
    # installation's pkg-config dependencies. Check its actual (possibly cached)
    # result and the libcrypto.pc provider required by openssl.pc.
    if(_OPENSSL_FOUND)
        foreach(metadata IN ITEMS _OPENSSL CHIBI_CRYPTO)
            file(REAL_PATH "${${metadata}_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX}" metadata_library)
            file(REAL_PATH "${${metadata}_INCLUDEDIR}" metadata_include)
            if(NOT ${metadata}_LIBDIR OR NOT ${metadata}_INCLUDEDIR OR
               NOT metadata_library STREQUAL selected_library OR NOT metadata_include STREQUAL selected_include)
                message(FATAL_ERROR "pkg-config OpenSSL metadata does not match s2n's selected crypto library/headers. "
                    "Selected: ${selected_library}, ${selected_include}. "
                    "Metadata: ${metadata_library}, ${metadata_include}. "
                    "Align pkg-config with the selected provider or supply a crypto target with its link dependencies.")
            endif()
        endforeach()
    endif()
    get_target_property(links OpenSSL::Crypto INTERFACE_LINK_LIBRARIES)
    if(links)
        set_property(TARGET AWS::crypto PROPERTY INTERFACE_LINK_LIBRARIES "${links}")
    endif()
endfunction()
chibi_crypto_dependencies()
