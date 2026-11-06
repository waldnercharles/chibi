# CPU resident images and typed pixel artifacts have no desktop dependencies.
if(NOT TARGET chibi_image_catalog)
    add_library(chibi_image_catalog STATIC "${CHIBI_SDK_ROOT}/src/runtime/images/catalog.c"
        "${CHIBI_SDK_ROOT}/src/runtime/images/sky.c")
    add_library(Chibi::image_catalog ALIAS chibi_image_catalog)
    set_target_properties(chibi_image_catalog PROPERTIES EXPORT_NAME image_catalog)
    chibi_sdk_headers(chibi_image_catalog PUBLIC runtime/images/catalog.h runtime/images/table.h
        runtime/images/sky.h)
    target_link_libraries(chibi_image_catalog PUBLIC chibi_shader_runtime PRIVATE chibi_types chibi_support)
    if(UNIX)
        target_link_libraries(chibi_image_catalog PRIVATE m)
    endif()
    list(APPEND CHIBI_SDK_TARGETS chibi_image_catalog)
endif()
if(CHIBI_SDK_BUILD_COMPILER AND NOT TARGET chibi_image_preparation)
    if(NOT CHIBI_CUTE_MATH_INCLUDE_DIR)
        message(FATAL_ERROR "Image preparation requires the owning pinned Cute source headers")
    endif()
    # The exact pinned CPU algorithms used by Cute, compiled as C with private
    # symbols; no framework implementation or desktop archive is linked.
    # Repair stored-DEFLATE consumption and scanline bounds in a local copy.
    # Never modify the shared pinned source cache; fail if its patch context drifts.
    set(image_patch "${CHIBI_SDK_ROOT}/cmake/patches/cute-png-stored-blocks.patch")
    set(image_vendor "${CMAKE_CURRENT_BINARY_DIR}/private/image_preparation/vendor")
    get_filename_component(image_source_root
        "${CHIBI_CUTE_MATH_INCLUDE_DIR}/../libraries/cute" ABSOLUTE)
    file(READ "${image_source_root}/cute_png.h" image_original)
    file(WRITE "${image_vendor}/cute_png.h" "${image_original}")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
        "--directory=${image_vendor}" "${image_patch}"
        RESULT_VARIABLE image_patch_result ERROR_VARIABLE image_patch_error)
    if(NOT image_patch_result EQUAL 0)
        message(FATAL_ERROR "Could not patch PNG stored blocks: ${image_patch_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${image_patch}")
    foreach(format png jpg)
        set(image_source "${image_source_root}/cute_${format}.h")
        if(format STREQUAL "png")
            set(image_source "${image_vendor}/cute_png.h")
        endif()
        file(READ "${image_source}" code)
        string(REPLACE "cp_" "chibi_image_cp_" code "${code}")
        string(REPLACE "cj_" "chibi_image_cj_" code "${code}")
        string(REPLACE "CP_CRC_TABLE" "chibi_image_cp_crc_table" code "${code}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/private/image_preparation/vendor/image_${format}.h" "${code}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${image_source_root}/cute_${format}.h")
    endforeach()
    add_library(chibi_image_preparation STATIC "${CHIBI_SDK_ROOT}/src/authoring/image_prepare.c"
        "${CHIBI_SDK_ROOT}/src/authoring/image_import.c")
    add_library(Chibi::image_preparation ALIAS chibi_image_preparation)
    set_target_properties(chibi_image_preparation PROPERTIES EXPORT_NAME image_preparation)
    chibi_sdk_headers(chibi_image_preparation PUBLIC authoring/image_prepare.h authoring/image_import.h)
    target_include_directories(chibi_image_preparation SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/image_preparation")
    target_link_libraries(chibi_image_preparation PUBLIC chibi_image_catalog PRIVATE chibi_support)
    if(UNIX)
        target_link_libraries(chibi_image_preparation PRIVATE m)
    endif()
    list(APPEND CHIBI_SDK_TARGETS chibi_image_preparation)
endif()
