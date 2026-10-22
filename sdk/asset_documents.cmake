# Compiler-enabled document transport and compiled Chibi asset entry points.
# Recovery builds embed compiler-produced bootstrap resources; the authored graph
# regenerates changed source snapshots through tools/toolchain/AssetCompiler.chibi.
file(READ "${asset_compiler_resource}/manifest.json" asset_compiler_manifest)
foreach(product artifact resource)
    string(JSON expected GET "${asset_compiler_manifest}" "${product}_sha256")
    if(product STREQUAL "artifact")
        set(asset_product asset_compiler.chibip)
    else()
        set(asset_product asset_compiler_packaged.c)
    endif()
    file(SHA256 "${asset_compiler_resource}/${asset_product}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Asset compiler bootstrap checksum mismatch: ${asset_product}")
    endif()
endforeach()
# Build-time verification lets the independent compiler host bootstrap when
# authored inputs changed, while refusing a stale asset service or installation.
add_custom_target(chibi_asset_compiler_freshness
    COMMAND ${CMAKE_COMMAND} "-DROOT=${CHIBI_SDK_ROOT}" "-DRESOURCE=${asset_compiler_resource}"
        -P "${CHIBI_SDK_ROOT}/cmake/verify_source_snapshot.cmake" VERBATIM)
set(asset_compiler_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/assets")
file(MAKE_DIRECTORY "${asset_compiler_generated}")
configure_file("${asset_compiler_resource}/asset_compiler_packaged.c"
    "${asset_compiler_generated}/asset_compiler_packaged.c" COPYONLY)
add_custom_command(OUTPUT "${asset_compiler_generated}/asset_compiler_blob.inc"
    COMMAND ${CMAKE_COMMAND} "-DINPUT=${asset_compiler_resource}/asset_compiler.chibip"
        "-DOUTPUT=${asset_compiler_generated}/asset_compiler_blob.inc" "-DSYMBOL=asset_compiler_blob"
        -P "${CHIBI_SDK_ROOT}/cmake/embed_binary.cmake"
    DEPENDS "${asset_compiler_resource}/asset_compiler.chibip"
        "${CHIBI_SDK_ROOT}/cmake/embed_binary.cmake" "${CHIBI_SDK_ROOT}/cmake/generated_file.cmake" VERBATIM)
set_source_files_properties("${asset_compiler_generated}/asset_compiler_packaged.c" PROPERTIES
    GENERATED TRUE OBJECT_DEPENDS "${asset_compiler_generated}/asset_compiler_blob.inc")
add_library(chibi_asset_documents STATIC "${CHIBI_SDK_ROOT}/src/support/asset_document.c"
    "${CHIBI_SDK_ROOT}/src/authoring/asset_compiler_host.c"
    "${asset_compiler_generated}/asset_compiler_packaged.c")
if(NOT CHIBI_ASSET_COMPILER_RECOVERY)
    add_dependencies(chibi_asset_documents chibi_asset_compiler_freshness)
endif()
add_library(Chibi::asset_documents ALIAS chibi_asset_documents)
set_target_properties(chibi_asset_documents PROPERTIES EXPORT_NAME asset_documents)
chibi_sdk_headers(chibi_asset_documents PUBLIC support/asset_document.h authoring/asset_compiler_host.h)
target_link_libraries(chibi_asset_documents PUBLIC chibi_asset_values
    PRIVATE chibi_asset_json chibi_compiler_host chibi_support)
target_include_directories(chibi_asset_documents PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson")
list(APPEND CHIBI_SDK_TARGETS chibi_asset_documents)
install(FILES "${asset_compiler_resource}/asset_compiler.chibip" "${asset_compiler_resource}/manifest.json"
    "${asset_compiler_resource}/target.json" "${asset_compiler_resource}/sources.sha256"
    "${asset_compiler_resource}/inventory.txt" "${asset_compiler_resource}/inventory-directories.txt"
    DESTINATION share/chibi/assets/compiler)
