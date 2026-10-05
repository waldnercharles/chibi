# Keep s2n's provider selection, including supplied targets and shared fallback.
if(TARGET crypto OR TARGET AWS::crypto)
    include("${s2n_SOURCE_DIR}/cmake/modules/Findcrypto.cmake")
    return()
endif()
include("${s2n_SOURCE_DIR}/cmake/modules/Findcrypto.cmake")
if(NOT TARGET AWS::crypto OR NOT crypto_LIBRARY MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$")
    return()
endif()

# s2n 1.6.3 omits static libcrypto's private dependencies. Use CMake's
# OpenSSL metadata only for the same library and headers; never switch providers.
function(chibi_crypto_dependencies)
    # FindOpenSSL temporarily restricts library suffixes to archives. Discover
    # optional zlib first with the caller's normal policy: its archive may not
    # be PIC, while libcrypto itself is also used inside the reload shared object.
    find_package(ZLIB QUIET)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
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
    get_target_property(links OpenSSL::Crypto INTERFACE_LINK_LIBRARIES)
    if(links)
        set_property(TARGET AWS::crypto PROPERTY INTERFACE_LINK_LIBRARIES "${links}")
    endif()
endfunction()
chibi_crypto_dependencies()
