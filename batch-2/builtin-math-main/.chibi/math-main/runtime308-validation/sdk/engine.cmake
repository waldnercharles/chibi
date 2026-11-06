# Visibility sampling is reusable without an engine or display host.
add_library(chibi_visibility_runtime STATIC
    "${CHIBI_SDK_ROOT}/src/runtime/visibility.c"
    "${CHIBI_SDK_ROOT}/src/runtime/visibility_definition.c")
add_library(Chibi::visibility_runtime ALIAS chibi_visibility_runtime)
set_target_properties(chibi_visibility_runtime PROPERTIES EXPORT_NAME visibility_runtime)
chibi_sdk_headers(chibi_visibility_runtime PUBLIC runtime/visibility.h runtime/visibility_definition.h)
target_link_libraries(chibi_visibility_runtime PUBLIC chibi_entity_runtime chibi_native_types
    chibi_native_capabilities PRIVATE chibi_physics_runtime chibi_spatial_runtime chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_visibility_runtime)

# Simulation consumes installed runtime services; display hosts inject rendering
# and source clocks. Authoring preview composition is outside this archive.
add_library(chibi_engine STATIC
    "${CHIBI_SDK_ROOT}/src/runtime/engine.c"
    "${CHIBI_SDK_ROOT}/src/runtime/world.c"
    "${CHIBI_SDK_ROOT}/src/runtime/particle_emitters.c")
add_library(Chibi::engine ALIAS chibi_engine)
set_target_properties(chibi_engine PROPERTIES EXPORT_NAME engine)
chibi_sdk_headers(chibi_engine PUBLIC runtime/engine.h runtime/particle_emitters.h
    runtime/world.h)
set(engine_private "${CMAKE_CURRENT_BINARY_DIR}/private/engine")
foreach(header diagnostics/tracy.h diagnostics/web_trace.h support/cos_sin.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${engine_private}/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_engine PRIVATE "${engine_private}")
target_link_libraries(chibi_engine PUBLIC chibi_behavior_runtime chibi_entity_runtime
    chibi_native_types chibi_scheduling chibi_physics_runtime chibi_physics3d_runtime chibi_material_runtime
    chibi_visibility_runtime chibi_particle_runtime chibi_audio_runtime chibi_url_runtime chibi_ui_runtime chibi_save_runtime chibi_input_runtime
    PRIVATE chibi_task_pool_runtime chibi_spatial_runtime
    chibi_execution chibi_support)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_engine PRIVATE -ffp-contract=off)
endif()
if(COMMAND chibi_enable_tracy)
    chibi_enable_tracy(chibi_engine)
endif()
list(APPEND CHIBI_SDK_TARGETS chibi_engine)

# The component types a project declares, shared by the player and authoring.
add_library(chibi_declared_components STATIC "${CHIBI_SDK_ROOT}/src/runtime/declared_components.c")
add_library(Chibi::declared_components ALIAS chibi_declared_components)
set_target_properties(chibi_declared_components PROPERTIES EXPORT_NAME declared_components)
chibi_sdk_headers(chibi_declared_components PUBLIC runtime/declared_components.h)
target_link_libraries(chibi_declared_components PUBLIC chibi_native_types chibi_capabilities
    chibi_execution chibi_types PRIVATE chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_declared_components)

add_library(chibi_player STATIC "${CHIBI_SDK_ROOT}/src/runtime/player.c"
    "${CHIBI_SDK_ROOT}/src/runtime/player_prepared.c")
add_library(Chibi::player ALIAS chibi_player)
set_target_properties(chibi_player PROPERTIES EXPORT_NAME player)
chibi_sdk_headers(chibi_player PUBLIC runtime/player.h runtime/player_prepared.h)
target_link_libraries(chibi_player PUBLIC chibi_engine chibi_behavior_runtime chibi_scene_runtime
    chibi_field_catalog chibi_declared_components)
list(APPEND CHIBI_SDK_TARGETS chibi_player)
