# Optional material simulation owns its algorithms and concrete ECS adapter.
# Authoring parameter text is prepared outside this target into field arguments.
set(chibi_material_sources
    materials.c materials_native.c materials_definition.c
    material_mask.c material_terrain.c material_fluid.c material_flow.c
    material_mpm.c material_obstacle_index.c material_particle_index.c
    material_destruction.c material_ecs.c material_ecs_terrain.c material_ecs_surface.c
    material_checkpoint.c terrain_storage.c terrain_storage_definition.c
    material_stream.c material_ecs_fluid.c material_debris.c material_ecs_debris.c
    geometry/material_surface_mesh.c geometry/distance_transform.c geometry/terrain_volume.c)
list(TRANSFORM chibi_material_sources PREPEND "${CHIBI_SDK_ROOT}/src/runtime/")
add_library(chibi_material_runtime STATIC ${chibi_material_sources})
add_library(Chibi::material_runtime ALIAS chibi_material_runtime)
set_target_properties(chibi_material_runtime PROPERTIES EXPORT_NAME material_runtime)
chibi_sdk_headers(chibi_material_runtime PUBLIC
    runtime/materials.h runtime/materials_native.h runtime/materials_definition.h
    runtime/material_mask.h runtime/material_terrain.h runtime/material_fluid.h
    runtime/material_destruction.h runtime/material_stream.h runtime/material_debris.h runtime/material_checkpoint.h
    runtime/terrain_storage.h runtime/terrain_storage_definition.h
    runtime/geometry/material_surface_mesh.h runtime/geometry/distance_transform.h
    runtime/geometry/terrain_volume.h)
set(material_private "${CMAKE_CURRENT_BINARY_DIR}/private/material_runtime")
foreach(header runtime/material_ecs.h runtime/material_flow.h runtime/material_mpm.h
        runtime/material_obstacle_index.h runtime/material_particle_index.h
        support/cos_sin.h support/exponential.h diagnostics/tracy.h diagnostics/web_trace.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${material_private}/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_material_runtime PRIVATE "${material_private}")
target_link_libraries(chibi_material_runtime PUBLIC chibi_ecs chibi_tasks
    chibi_field_catalog chibi_native_capabilities chibi_mesh_runtime PRIVATE chibi_native_types chibi_shader_runtime
    chibi_entity_runtime chibi_particle_runtime chibi_spatial_runtime
    chibi_task_pool_runtime chibi_execution chibi_support chibi_save_runtime)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_material_runtime PRIVATE -ffp-contract=off)
endif()
if(COMMAND chibi_enable_tracy)
    chibi_enable_tracy(chibi_material_runtime)
endif()
list(APPEND CHIBI_SDK_TARGETS chibi_material_runtime)
