# Compiler-enabled document transport and compiled Chibi asset entry points.
# Recovery builds embed compiler-produced bootstrap resources; the authored graph
# regenerates changed source snapshots through tools/graph/AssetCompiler.chibi.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(asset_compiler_variant wasm32)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(asset_compiler_variant native64)
else()
    message(FATAL_ERROR "No asset compiler bootstrap resource for this target")
endif()
set(asset_compiler_resource "${CHIBI_SDK_ROOT}/tools/compiler/asset_compiler/bootstrap/${asset_compiler_variant}")
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
add_library(Chibi::asset_documents ALIAS chibi_asset_documents)
set_target_properties(chibi_asset_documents PROPERTIES EXPORT_NAME asset_documents)
chibi_sdk_headers(chibi_asset_documents PUBLIC support/asset_document.h authoring/asset_compiler_host.h)
target_link_libraries(chibi_asset_documents PUBLIC chibi_asset_values
    PRIVATE chibi_asset_json chibi_compiler_host chibi_support)
target_include_directories(chibi_asset_documents PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson")
list(APPEND CHIBI_SDK_TARGETS chibi_asset_documents)
install(FILES "${asset_compiler_resource}/asset_compiler.chibip" "${asset_compiler_resource}/manifest.json"
    "${asset_compiler_resource}/target.json" "${asset_compiler_resource}/sources.sha256"
    DESTINATION share/chibi/assets/compiler)
