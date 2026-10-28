# Native source-built authored asset service. No checked asset bootstrap is used.
set(asset_output "${CMAKE_CURRENT_BINARY_DIR}/generated/assets")
set(asset_generator_timeout 240000)
set(asset_compiler_timeout_arguments "")
if(CHIBI_ENABLE_SANITIZERS)
    set(asset_generator_timeout 600000)
    list(APPEND asset_compiler_timeout_arguments --compiler-timeout-ms=600000)
endif()
# Another build's sdk directory whose hosts generate the resource instead of
# this build's, such as a Release build's for a sanitized one: the resource is
# the same whichever build's hosts generate it.
set(CHIBI_ASSET_GENERATOR_HOSTS "" CACHE PATH
    "Directory with the chibi-build-host and chibi-compiler-host that generate the asset compiler")
set(asset_build_host $<TARGET_FILE:chibi-build-host>)
set(asset_compiler_host $<TARGET_FILE:chibi-compiler-host>)
set(asset_hosts chibi-build-host chibi-compiler-host)
if(CHIBI_ASSET_GENERATOR_HOSTS)
    set(asset_build_host "${CHIBI_ASSET_GENERATOR_HOSTS}/chibi-build-host${CMAKE_EXECUTABLE_SUFFIX}")
    set(asset_compiler_host "${CHIBI_ASSET_GENERATOR_HOSTS}/chibi-compiler-host${CMAKE_EXECUTABLE_SUFFIX}")
    set(asset_hosts "${asset_build_host}" "${asset_compiler_host}")
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
        --collection asset_generator=tools/graph/asset_compiler --entry Build --timeout-ms ${asset_generator_timeout} --
        "${asset_compiler_host}" "${asset_output}" ${asset_compiler_timeout_arguments}
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
