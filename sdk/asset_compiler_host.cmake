# Native source-built authored asset service. No checked asset bootstrap is used.
set(asset_output "${CMAKE_CURRENT_BINARY_DIR}/generated/assets")
# Debug hosts compile the asset compiler two times slower than Release ones and
# sanitized hosts twelve times, so those builds run the generator with Release
# hosts built beside them.
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CHIBI_ENABLE_SANITIZERS)
    set(asset_build_host $<TARGET_FILE:chibi-build-host>)
    set(asset_compiler_host $<TARGET_FILE:chibi-compiler-host>)
    set(asset_hosts chibi-build-host chibi-compiler-host)
else()
    include(ExternalProject)
    set(release_hosts "${CMAKE_CURRENT_BINARY_DIR}/release-hosts")
    set(asset_build_host "${release_hosts}/chibi-build-host")
    set(asset_compiler_host "${release_hosts}/chibi-compiler-host")
    set(release_host_settings -DCMAKE_BUILD_TYPE:STRING=Release
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        -DCHIBI_SDK_BUILD_NATIVE:BOOL=OFF -DCHIBI_SDK_BUILD_DESKTOP:BOOL=OFF)
    foreach(setting CMAKE_C_COMPILER_LAUNCHER CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES
            CHIBI_THIRD_PARTY_CACHE)
        if(${setting})
            list(APPEND release_host_settings "-D${setting}:STRING=${${setting}}")
        endif()
    endforeach()
    ExternalProject_Add(chibi_release_hosts
        SOURCE_DIR "${CHIBI_SDK_ROOT}/sdk"
        BINARY_DIR "${release_hosts}"
        CMAKE_CACHE_ARGS ${release_host_settings}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target chibi-build-host chibi-compiler-host
        BUILD_ALWAYS ON
        BUILD_BYPRODUCTS "${asset_build_host}" "${asset_compiler_host}"
        INSTALL_COMMAND "")
    set(asset_hosts chibi_release_hosts "${asset_build_host}" "${asset_compiler_host}")
endif()
file(GLOB_RECURSE asset_sources CONFIGURE_DEPENDS
    "${CHIBI_SDK_ROOT}/tools/compiler/*.chibi"
    "${CHIBI_SDK_ROOT}/packages/core/*.chibi"
    "${CHIBI_SDK_ROOT}/packages/game/*.chibi"
    "${CHIBI_SDK_ROOT}/tools/graph/asset_compiler/*.chibi")
add_custom_command(
    OUTPUT "${asset_output}/asset_compiler_packaged.c" "${asset_output}/asset_compiler_blob.inc"
        "${asset_output}/asset_compiler.chibip" "${asset_output}/manifest.json"
        "${asset_output}/sources.sha256" "${asset_output}/inventory.txt"
        "${asset_output}/inventory-directories.txt" "${asset_output}/target.json"
    COMMAND "${asset_build_host}"
        --compiler tools/compiler --compiler-core packages/core --root "${CHIBI_SDK_ROOT}"
        --source tools/toolchain/AssetCompiler.chibi --core packages/core
        --collection asset_generator=tools/graph/asset_compiler --entry Build --timeout-ms 240000 --
        "${asset_compiler_host}" "${asset_output}"
    COMMAND "${CMAKE_COMMAND}" -DROOT=${CHIBI_SDK_ROOT} -DRESOURCE=${asset_output}
        -P "${CHIBI_SDK_ROOT}/cmake/verify_source_snapshot.cmake"
    WORKING_DIRECTORY "${CHIBI_SDK_ROOT}"
    DEPENDS ${asset_hosts} ${asset_sources}
        "${CHIBI_SDK_ROOT}/tools/compiler/bootstrap/compiler.chibip"
        "${CHIBI_SDK_ROOT}/tools/compiler/bootstrap/bootstrap.json"
        "${CHIBI_SDK_ROOT}/tools/compiler/asset_compiler/CompilerSources.txt"
        "${CHIBI_SDK_ROOT}/tools/toolchain/AssetCompiler.chibi"
        "${CHIBI_SDK_ROOT}/cmake/verify_source_snapshot.cmake"
    VERBATIM)
add_library(chibi_asset_compiler_host STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/asset_compiler_host.c"
    "${CHIBI_SDK_ROOT}/src/support/binary_cache.c"
    "${asset_output}/asset_compiler_packaged.c")
add_library(Chibi::asset_compiler_host ALIAS chibi_asset_compiler_host)
set_target_properties(chibi_asset_compiler_host PROPERTIES EXPORT_NAME asset_compiler_host)
chibi_sdk_headers(chibi_asset_compiler_host PUBLIC authoring/asset_compiler_host.h)
foreach(header authoring/asset_compiler_host_internal.h support/binary_cache.h support/sha256.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}"
        "${CMAKE_CURRENT_BINARY_DIR}/private/asset_compiler_host/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_asset_compiler_host PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/private/asset_compiler_host" "${asset_output}"
    "${CHIBI_SDK_ROOT}/third_party/yyjson")
target_link_libraries(chibi_asset_compiler_host PUBLIC chibi_asset_values
    PRIVATE chibi_compiler_host chibi_asset_json chibi_json)
list(APPEND CHIBI_SDK_TARGETS chibi_asset_compiler_host)
# Reject installing a resource compiled from a stale authored source snapshot.
install(CODE "set(ROOT \"${CHIBI_SDK_ROOT}\")\nset(RESOURCE \"${asset_output}\")\ninclude(\"${CHIBI_SDK_ROOT}/cmake/verify_source_snapshot.cmake\")")
