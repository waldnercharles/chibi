# Real physics and task dependencies; no Cute renderer or SDL target enters this closure.
# Box2D and Box3D come prebuilt from the third-party archive. Physics's public
# ABI exposes neither; Box3D is alpha, and Cute links the same v0.1.0.
chibi_sdk_vendor(chibi_box2d box2d box2d)
chibi_sdk_vendor(chibi_box3d box3d box3d)

add_library(chibi_task_pool_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/task_pool.c")
add_library(Chibi::task_pool_runtime ALIAS chibi_task_pool_runtime)
set_target_properties(chibi_task_pool_runtime PROPERTIES EXPORT_NAME task_pool_runtime)
chibi_sdk_headers(chibi_task_pool_runtime PUBLIC runtime/task_pool.h)
target_link_libraries(chibi_task_pool_runtime PUBLIC chibi_tasks PRIVATE chibi_support)
set(trace_root "${CMAKE_CURRENT_BINARY_DIR}/private/physics")
foreach(header tracy.h web_trace.h)
    configure_file("${CHIBI_SDK_ROOT}/src/diagnostics/${header}"
        "${trace_root}/diagnostics/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_task_pool_runtime PRIVATE "${trace_root}")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_THREADS)
    # enkiTS is C++, so its consumers in this directory link with the C++ driver.
    enable_language(CXX)
    chibi_sdk_vendor(chibi_task_scheduler task_scheduler enkits)
    target_link_libraries(chibi_task_pool_runtime PRIVATE chibi_task_scheduler)
    target_compile_definitions(chibi_task_pool_runtime PRIVATE CHIBI_HAS_ENKITS=1)
    if(CHIBI_WEB_THREADS)
        target_compile_definitions(chibi_task_pool_runtime PRIVATE CHIBI_WEB_WORKERS=${CHIBI_WEB_WORKERS})
    endif()
    list(APPEND CHIBI_SDK_TARGETS chibi_task_scheduler)
endif()

foreach(dependency chibi_native_types chibi_entity_runtime chibi_material_runtime)
    if(NOT TARGET ${dependency})
        message(FATAL_ERROR "Physics SDK package requires owning target ${dependency}")
    endif()
endforeach()
add_library(chibi_physics_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/scenes/physics.c"
    "${CHIBI_SDK_ROOT}/src/runtime/physics_native.c"
    "${CHIBI_SDK_ROOT}/src/runtime/physics_definition.c")
add_library(Chibi::physics_runtime ALIAS chibi_physics_runtime)
set_target_properties(chibi_physics_runtime PROPERTIES EXPORT_NAME physics_runtime)
chibi_sdk_headers(chibi_physics_runtime PUBLIC runtime/scenes/physics.h
    runtime/scenes/physics_settings.h runtime/physics_native.h runtime/physics_definition.h)
target_include_directories(chibi_physics_runtime PRIVATE "${trace_root}")
target_link_libraries(chibi_physics_runtime PUBLIC chibi_native_types chibi_entity_runtime
    chibi_material_runtime chibi_task_pool_runtime chibi_native_capabilities
    PRIVATE chibi_spatial_runtime chibi_execution chibi_support chibi_box2d)
add_library(chibi_physics3d_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/scenes/physics3d.c"
    "${CHIBI_SDK_ROOT}/src/runtime/physics3d_native.c"
    "${CHIBI_SDK_ROOT}/src/runtime/physics3d_definition.c")
add_library(Chibi::physics3d_runtime ALIAS chibi_physics3d_runtime)
set_target_properties(chibi_physics3d_runtime PROPERTIES EXPORT_NAME physics3d_runtime)
chibi_sdk_headers(chibi_physics3d_runtime PUBLIC runtime/scenes/physics3d.h
    runtime/physics3d_native.h runtime/physics3d_definition.h)
# Like Box2D, Box3D stays private: the public header declares none of it.
target_link_libraries(chibi_physics3d_runtime PUBLIC chibi_native_types chibi_entity_runtime
    chibi_native_capabilities PRIVATE chibi_task_pool_runtime chibi_spatial_runtime chibi_mesh_runtime chibi_execution
    chibi_support chibi_box3d)
if(COMMAND chibi_enable_tracy)
    chibi_enable_tracy(chibi_task_pool_runtime)
    chibi_enable_tracy(chibi_physics_runtime)
endif()
list(APPEND CHIBI_SDK_TARGETS chibi_box2d chibi_box3d chibi_task_pool_runtime chibi_physics_runtime
    chibi_physics3d_runtime)
