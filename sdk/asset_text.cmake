# Authoring parses declarative KDL through the same asset vocabulary as Studio.
# The parser has no exported headers and is absent from runtime-only packages.
set(kdl_root "${CHIBI_SDK_ROOT}/third_party/ckdl")
add_library(chibi_asset_kdl STATIC
    "${kdl_root}/src/bigint.c" "${kdl_root}/src/compat.c"
    "${kdl_root}/src/emitter.c" "${kdl_root}/src/parser.c"
    "${kdl_root}/src/str.c" "${kdl_root}/src/tokenizer.c" "${kdl_root}/src/utf8.c")
set_target_properties(chibi_asset_kdl PROPERTIES EXPORT_NAME asset_kdl)
target_include_directories(chibi_asset_kdl PRIVATE "${kdl_root}/include" "${kdl_root}/src")
target_compile_definitions(chibi_asset_kdl PRIVATE KDL_STATIC_LIB=1 BUILDING_KDL=1)
target_compile_options(chibi_asset_kdl PRIVATE -w)
install(FILES "${kdl_root}/COPYING" DESTINATION share/chibi/licenses/ckdl)

configure_file("${CHIBI_SDK_ROOT}/src/support/float_text.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/asset_text/support/float_text.h" COPYONLY)
add_library(chibi_asset_text STATIC "${CHIBI_SDK_ROOT}/src/support/asset_text.c")
add_library(Chibi::asset_text ALIAS chibi_asset_text)
set_target_properties(chibi_asset_text PROPERTIES EXPORT_NAME asset_text)
chibi_sdk_headers(chibi_asset_text PUBLIC support/asset_text.h)
target_include_directories(chibi_asset_text PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/private/asset_text" "${kdl_root}/include")
target_compile_definitions(chibi_asset_text PRIVATE KDL_STATIC_LIB=1)
target_link_libraries(chibi_asset_text PUBLIC chibi_asset_values PRIVATE chibi_asset_kdl)
list(APPEND CHIBI_SDK_TARGETS chibi_asset_text chibi_asset_kdl)
