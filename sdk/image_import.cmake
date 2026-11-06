# CPU image import planning and atomic publication have no desktop dependency.
add_library(chibi_image_import STATIC "${CHIBI_SDK_ROOT}/src/authoring/image_import_commit.c")
add_library(Chibi::image_import ALIAS chibi_image_import)
set_target_properties(chibi_image_import PROPERTIES EXPORT_NAME image_import)
chibi_sdk_headers(chibi_image_import PUBLIC authoring/image_import_commit.h)
target_link_libraries(chibi_image_import PUBLIC chibi_image_preparation chibi_shader_preparation
    PRIVATE chibi_asset_text chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_image_import)
