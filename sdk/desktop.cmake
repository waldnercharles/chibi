# Optional desktop execution/presentation packages. The source authoring side is
# owned by project_preparation; these targets consume prepared runtime artifacts.
enable_language(CXX)
if(APPLE)
    enable_language(OBJC)
endif()
# Cute (with SDL) and RmlUi (with FreeType) come prebuilt from the third-party
# archive (cmake/third_party.cmake); the SDK installs copies of their archives.
chibi_vendor_dependency(Chibi::vendor_cute CHIBI_DESKTOP_CUTE)
chibi_vendor_dependency(Chibi::vendor_rmlui_core CHIBI_DESKTOP_RMLUI)

add_library(chibi_platform STATIC "${CHIBI_SDK_ROOT}/src/support/resources.c")
add_library(Chibi::platform ALIAS chibi_platform)
set_target_properties(chibi_platform PROPERTIES EXPORT_NAME platform)
chibi_sdk_headers(chibi_platform PUBLIC platform/cute.h support/resources.h)
target_link_libraries(chibi_platform PUBLIC
    ${CHIBI_DESKTOP_CUTE})

configure_file("${CHIBI_SDK_ROOT}/src/assets/project.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/platform/assets/project.h" COPYONLY)
target_include_directories(chibi_platform PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/platform")
target_link_libraries(chibi_platform PRIVATE chibi_json chibi_support)
target_compile_definitions(chibi_platform PRIVATE CHIBI_DISTRIBUTION)
if(EMSCRIPTEN)
    target_compile_definitions(chibi_platform PRIVATE CHIBI_SOURCE_DIR="/resources")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/images.cmake")


# The retained UI host, native controls and embedded faces are shared with Studio.
set(ui_sources rml.cpp json_items.c rml_backend.c rml_gpu.c iframe.c motion.c text_motion.c
    controls/controls.c controls/choice_list.c controls/color_field.c controls/focus.c
    controls/line_chart.c controls/line_text.c controls/live_text.c controls/number_field.c
    controls/splitter.c controls/tab_list.c popover.c drag.c list.c focus_within.c autofocus.c)
list(TRANSFORM ui_sources PREPEND "${CHIBI_SDK_ROOT}/src/ui/")
list(APPEND ui_sources "${CHIBI_SDK_ROOT}/src/support/ui_style.c"
    "${CHIBI_SDK_ROOT}/src/support/number_expr.c")
add_library(chibi_ui_host STATIC ${ui_sources})
add_library(Chibi::ui ALIAS chibi_ui_host)
set_target_properties(chibi_ui_host PROPERTIES EXPORT_NAME ui CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
chibi_sdk_headers(chibi_ui_host PUBLIC ui/rml.h ui/rml_backend.h ui/rml_gpu.h ui/iframe.h
    ui/iframe_profile.h ui/json_items.h ui/motion.h support/ui_style.h support/number_expr.h ui/controls/controls.h
    ui/popover.h ui/drag.h ui/list.h ui/focus_within.h ui/autofocus.h ui/input_feel.h)
set(ui_private "${CMAKE_CURRENT_BINARY_DIR}/private/ui")
foreach(header ui/controls/controls_internal.h support/math.h diagnostics/tracy.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${ui_private}/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_ui_host PRIVATE "${ui_private}")
target_link_libraries(chibi_ui_host PUBLIC chibi_platform chibi_json chibi_ui_document_values chibi_ui_documents chibi_text_search
    PRIVATE chibi_builtin_fonts chibi_support ${CHIBI_DESKTOP_RMLUI})
if(APPLE)
    target_link_libraries(chibi_ui_host PRIVATE c++)
elseif(UNIX AND NOT EMSCRIPTEN)
    target_link_libraries(chibi_ui_host PRIVATE stdc++)
endif()


set(render_sources appearance_geometry.c scene_renderer.c shader_fragment.c shader_preview.c
    shader_publish.c screenshot.c material.c material_lod.c material_mesh.c material_height.c spot_lights.c shader_images.c
    shader_program.c fog.c material_raster.c fluid_raster.c
    reflections.c radiance.c post_2d.c gpu_timing.c viewport.c audio.c audio_input.c audio_web.c geometry_2d.c grid_2d.c
    mesh_surface.c mesh_material.c scene3d.c scene3d_geometry.c scene3d_influence.c scene3d_mesh_cache.c scene3d_terrain.c scene3d_terrain_volume.c scene3d_volume.c
    scene3d_visual.c scene3d_trail.c scene3d_particles.c scene3d_camera.c scene3d_pick.c scene3d_post.c scene3d_shadow.c scene3d_occlusion.c scene3d_cascades.c scene3d_gi.c
    scene3d_light_shadow.c scene3d_sky.c field_image.c ui.c device_controls.c)
list(TRANSFORM render_sources PREPEND "${CHIBI_SDK_ROOT}/src/render/")
add_library(chibi_renderer STATIC ${render_sources}
    "${CHIBI_SDK_ROOT}/src/runtime/camera.c")
add_library(Chibi::render ALIAS chibi_renderer)
set_target_properties(chibi_renderer PROPERTIES EXPORT_NAME render)
file(GLOB render_headers RELATIVE "${CHIBI_SDK_ROOT}/src" "${CHIBI_SDK_ROOT}/src/render/*.h")
list(REMOVE_ITEM render_headers render/shader_catalog_internal.h render/scene3d_parts.h render/scene3d_parts_table.h render/catalog_publication.h render/shader_catalog.h render/shader_catalog_records.h render/shader_values.h render/shader_parameters.h)
chibi_sdk_headers(chibi_renderer PUBLIC ${render_headers} support/file_watch.h runtime/camera.h)
set(render_private "${CMAKE_CURRENT_BINARY_DIR}/private/render")
file(REMOVE "${render_private}/render/shader_catalog_internal.h"
    "${render_private}/render/scene3d_parts_internal.h" "${render_private}/render/shader_publish_internal.h")
foreach(header diagnostics/tracy.h support/math.h third_party/yyjson/yyjson.h)
    if(header STREQUAL "third_party/yyjson/yyjson.h")
        configure_file("${CHIBI_SDK_ROOT}/${header}" "${render_private}/${header}" COPYONLY)
    else()
        configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${render_private}/${header}" COPYONLY)
    endif()
endforeach()
target_include_directories(chibi_renderer PRIVATE "${render_private}")
target_link_libraries(chibi_renderer PUBLIC chibi_platform chibi_ui_host chibi_engine chibi_player
    chibi_shader_runtime chibi_model_runtime chibi_mesh_runtime chibi_native_types chibi_spatial_runtime chibi_native_capabilities chibi_particle_runtime chibi_material_runtime
    chibi_ui_runtime chibi_save_runtime chibi_ui_document_values chibi_ui_documents chibi_visual_catalog
    PRIVATE chibi_support chibi_json chibi_asset_values chibi_task_pool_runtime chibi_rhythm_runtime chibi_tilemap_runtime)
if(APPLE)
    target_sources(chibi_renderer PRIVATE "${CHIBI_SDK_ROOT}/src/render/gpu_clock.m")
    target_link_libraries(chibi_renderer PRIVATE "-framework Metal" "-framework Foundation")
else()
    target_sources(chibi_renderer PRIVATE "${CHIBI_SDK_ROOT}/src/render/gpu_clock.c")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_renderer PRIVATE -Wno-macro-redefined)
endif()
# Actual GPU programs are installed beside the engine, independent of Studio assets.
install(DIRECTORY "${CHIBI_SDK_ROOT}/src/render/shaders/" DESTINATION share/chibi/shaders)
list(APPEND CHIBI_SDK_TARGETS chibi_platform chibi_ui_host chibi_renderer)

# The deployed player links only prepared runtime packages. The web SDK
# builds its browser player only on request, outside the SDK package.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_PLAYER)
    add_executable(chibi-player "${CHIBI_SDK_ROOT}/src/host/player_main.c"
        "${CHIBI_SDK_ROOT}/src/host/player_url.c" "${CHIBI_SDK_ROOT}/src/host/native_libraries.c"
        "${CHIBI_SDK_ROOT}/src/host/stack_overflow.c")
    set(player_private "${CMAKE_CURRENT_BINARY_DIR}/private/player")
    foreach(header player_url.h player_clock.h native_libraries.h stack_overflow.h)
        configure_file("${CHIBI_SDK_ROOT}/src/host/${header}" "${player_private}/host/${header}" COPYONLY)
    endforeach()
    target_include_directories(chibi-player PRIVATE "${player_private}")
    target_link_libraries(chibi-player PRIVATE Chibi::render Chibi::player
        Chibi::native_prepared Chibi::host_native_default Chibi::text_capabilities ${CMAKE_DL_LIBS})
    set_target_properties(chibi-player PROPERTIES LINKER_LANGUAGE CXX)
endif()
if(CHIBI_WEB_PLAYER)
    set(web "${CHIBI_SDK_ROOT}/src/host/web")
    set_target_properties(chibi-player PROPERTIES SUFFIX ".js")
    set_property(TARGET chibi-player APPEND PROPERTY LINK_DEPENDS "${web}/player_data.js")
    target_link_options(chibi-player PRIVATE "--pre-js=${web}/player_data.js"
        -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFORCE_FILESYSTEM=1)
    install(FILES "$<TARGET_FILE:chibi-player>" "$<TARGET_FILE_DIR:chibi-player>/chibi-player.wasm"
        "${web}/index.html" DESTINATION . COMPONENT web_player EXCLUDE_FROM_ALL)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    chibi_windows_manifest(chibi-player)
    chibi_install_product(chibi-player)
endif()


target_link_libraries(chibi_renderer PUBLIC chibi_image_catalog)
