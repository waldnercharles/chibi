# Neutral immutable shader IR, evaluation and emission used by models and render.
if(TARGET chibi_shader_runtime)
    return()
endif()
add_library(chibi_shader_runtime STATIC
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/bake.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/program.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/prepared.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/detail.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/effects.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/image.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/volume.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/inspection.c"
    "${CHIBI_SDK_ROOT}/src/runtime/shaders/source_debug.c")
add_library(Chibi::shader_runtime ALIAS chibi_shader_runtime)
set_target_properties(chibi_shader_runtime PROPERTIES EXPORT_NAME shader_runtime)
chibi_sdk_headers(chibi_shader_runtime PUBLIC runtime/shaders/bake.h runtime/shaders/program.h runtime/shaders/prepared.h runtime/shaders/volume.h
    runtime/shaders/detail.h runtime/shaders/effects.h runtime/shaders/image.h runtime/shaders/inspection.h
    runtime/shaders/source_debug.h)
configure_file("${CHIBI_SDK_ROOT}/src/runtime/shaders/prepared_internal.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/shader_runtime/runtime/shaders/prepared_internal.h" COPYONLY)
target_include_directories(chibi_shader_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/shader_runtime")
target_link_libraries(chibi_shader_runtime PUBLIC chibi_support PRIVATE chibi_types)
if(UNIX)
    target_link_libraries(chibi_shader_runtime PRIVATE m)
endif()

list(APPEND CHIBI_SDK_TARGETS chibi_shader_runtime)
