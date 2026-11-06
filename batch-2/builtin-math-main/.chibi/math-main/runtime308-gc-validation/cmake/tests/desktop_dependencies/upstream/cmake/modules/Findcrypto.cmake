# Model s2n's imported-target scope and existing-provider branch without lookup.
if(TARGET crypto OR TARGET AWS::crypto)
    if(TARGET crypto)
        set(provider crypto)
    else()
        set(provider AWS::crypto)
    endif()
    get_target_property(crypto_INCLUDE_DIR "${provider}" INTERFACE_INCLUDE_DIRECTORIES)
    set(crypto_FOUND TRUE)
    set(CRYPTO_FOUND TRUE)
    return()
endif()
set(crypto_LIBRARY "${FIXTURE_PROVIDER_ROOT}/lib/libcrypto.a")
set(crypto_INCLUDE_DIR "${FIXTURE_PROVIDER_ROOT}/include/crypto")
set(crypto_FOUND TRUE)
set(CRYPTO_FOUND TRUE)
find_package(Threads REQUIRED)
add_library(AWS::crypto UNKNOWN IMPORTED)
set_target_properties(AWS::crypto PROPERTIES
    IMPORTED_LOCATION "${crypto_LIBRARY}"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    INTERFACE_INCLUDE_DIRECTORIES "${crypto_INCLUDE_DIR}")
add_dependencies(AWS::crypto Threads::Threads)
