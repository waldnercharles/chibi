if(TARGET chibi_shader_compiler)
    return()
endif()

set(shader_vendor "${CHIBI_CUTE_MATH_INCLUDE_DIR}/..")
add_library(chibi_shader_compiler STATIC "${CHIBI_SDK_ROOT}/src/render/shader_text.c")
add_library(Chibi::shader_compiler ALIAS chibi_shader_compiler)
set_target_properties(chibi_shader_compiler PROPERTIES EXPORT_NAME shader_compiler)
chibi_sdk_headers(chibi_shader_compiler PUBLIC render/shader_text.h)
target_link_libraries(chibi_shader_compiler PUBLIC chibi_shader_runtime PRIVATE chibi_support)
target_include_directories(chibi_shader_compiler SYSTEM PRIVATE
    "${shader_vendor}/include" "${shader_vendor}/tools")
target_include_directories(chibi_shader_compiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/shader_compiler")
configure_file("${CHIBI_SDK_ROOT}/src/render/shader_text_backend.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/shader_compiler/render/shader_text_backend.h" COPYONLY)

# Compile exactly the vertex and fragment templates used by the player, without
# creating a graphics device. Changes to either template regenerate this header.
set(particle_sources "")
foreach(stage vertex fragment)
    if(stage STREQUAL "vertex")
        set(extension vert)
    else()
        set(extension frag)
    endif()
    set(path "${CHIBI_SDK_ROOT}/src/render/shaders/scene3d_particles.${extension}")
    file(READ "${path}" code HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," code "${code}")
    string(APPEND particle_sources "static const char shader_particle_${stage}[] = {${code}0};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/private/shader_compiler/render/particle_sources.h" "${particle_sources}")

if(CHIBI_SDK_BUILD_DESKTOP)
    target_link_libraries(chibi_shader_compiler PRIVATE
        $<BUILD_INTERFACE:cute> $<INSTALL_INTERFACE:${CHIBI_DESKTOP_CUTE}>)
else()
    # Upstream's standalone compiler needs only its allocator and ckit. It does
    # not pull SDL, a graphics backend, or the desktop host into offline tools.
    enable_language(CXX)
    add_library(chibi_shader_vendor STATIC
        "${shader_vendor}/tools/cute_shader.cpp"
        "${shader_vendor}/tools/cute_shader_ckit.cpp"
        "${shader_vendor}/tools/cute_shader_assert.cpp"
        "${shader_vendor}/src/cute_alloc.cpp")
    set_target_properties(chibi_shader_vendor PROPERTIES EXPORT_NAME shader_vendor
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    target_include_directories(chibi_shader_vendor SYSTEM PRIVATE
        "${shader_vendor}/include" "${shader_vendor}/libraries" "${shader_vendor}/src")
    target_compile_options(chibi_shader_vendor PRIVATE -w)
    if(APPLE)
        target_link_libraries(chibi_shader_vendor PRIVATE c++)
    elseif(UNIX AND NOT EMSCRIPTEN)
        target_link_libraries(chibi_shader_vendor PRIVATE stdc++ m)
    endif()
    target_link_libraries(chibi_shader_compiler PRIVATE chibi_shader_vendor)
    list(APPEND CHIBI_SDK_TARGETS chibi_shader_vendor)
endif()
list(APPEND CHIBI_SDK_TARGETS chibi_shader_compiler)
