# Real physics and task dependencies; no Cute renderer or SDL target enters this closure.
set(box2d_root "${CHIBI_THIRD_PARTY_DIR}/source/box2d")
file(GLOB box2d_sources CONFIGURE_DEPENDS "${box2d_root}/src/*.c")
add_library(chibi_box2d STATIC ${box2d_sources})
add_library(Chibi::box2d ALIAS chibi_box2d)
set_target_properties(chibi_box2d PROPERTIES EXPORT_NAME box2d C_STANDARD 17
    C_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE ON)
target_include_directories(chibi_box2d PRIVATE "${box2d_root}/src")
# Physics's public ABI exposes no Box2D declarations.
target_include_directories(chibi_box2d INTERFACE $<BUILD_INTERFACE:${box2d_root}/include>)
target_include_directories(chibi_box2d PRIVATE "${box2d_root}/include")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_box2d PRIVATE -ffp-contract=off)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if(CHIBI_WEB_THREADS)
        target_compile_options(chibi_box2d PRIVATE -msimd128 -msse2)
    else()
        target_compile_definitions(chibi_box2d PRIVATE BOX2D_DISABLE_SIMD)
    endif()
endif()
if(UNIX)
    target_link_libraries(chibi_box2d PRIVATE m)
endif()
install(FILES "${box2d_root}/LICENSE" DESTINATION share/chibi/licenses/box2d)

# Box3D is alpha; Cute compiles the same vendored v0.1.0.
set(box3d_root "${CHIBI_THIRD_PARTY_DIR}/source/box3d")
file(GLOB box3d_sources CONFIGURE_DEPENDS "${box3d_root}/src/*.c")
add_library(chibi_box3d STATIC ${box3d_sources})
add_library(Chibi::box3d ALIAS chibi_box3d)
set_target_properties(chibi_box3d PROPERTIES EXPORT_NAME box3d C_STANDARD 17
    C_STANDARD_REQUIRED YES C_EXTENSIONS YES POSITION_INDEPENDENT_CODE ON)
target_include_directories(chibi_box3d PRIVATE "${box3d_root}/src" "${box3d_root}/include")
target_include_directories(chibi_box3d INTERFACE $<BUILD_INTERFACE:${box3d_root}/include>)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_box3d PRIVATE -ffp-contract=off)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if(CHIBI_WEB_THREADS)
        target_compile_options(chibi_box3d PRIVATE -msimd128 -msse2)
    else()
        target_compile_definitions(chibi_box3d PRIVATE BOX3D_DISABLE_SIMD)
    endif()
endif()
if(UNIX)
    target_link_libraries(chibi_box3d PRIVATE m)
endif()
# Box3D's internal scheduler and timers use the platform thread API.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(chibi_box3d PRIVATE Threads::Threads)
endif()
install(FILES "${box3d_root}/LICENSE" DESTINATION share/chibi/licenses/box3d)

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
    set(enkits_root "${CHIBI_THIRD_PARTY_DIR}/source/enkits")
    enable_language(CXX)
    find_package(Threads REQUIRED)
    set(CHIBI_SDK_NEEDS_THREADS ON)
    add_library(chibi_task_scheduler STATIC "${enkits_root}/src/TaskScheduler.cpp"
        "${enkits_root}/src/TaskScheduler_c.cpp")
    add_library(Chibi::task_scheduler ALIAS chibi_task_scheduler)
    set_target_properties(chibi_task_scheduler PROPERTIES EXPORT_NAME task_scheduler
        CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(chibi_task_scheduler PRIVATE "${enkits_root}/src")
    if(NOT DEFINED ENKITS_TASK_PRIORITIES_NUM)
        set(ENKITS_TASK_PRIORITIES_NUM 3)
    endif()
    if(ENKITS_TASK_PRIORITIES_NUM GREATER 0)
        target_compile_definitions(chibi_task_scheduler PRIVATE
            ENKITS_TASK_PRIORITIES_NUM=${ENKITS_TASK_PRIORITIES_NUM})
        target_compile_definitions(chibi_task_pool_runtime PRIVATE
            ENKITS_TASK_PRIORITIES_NUM=${ENKITS_TASK_PRIORITIES_NUM})
    endif()
    target_link_libraries(chibi_task_scheduler PRIVATE Threads::Threads)
    # Installed C-only consumers still need the vendor's C++ runtime.
    if(APPLE)
        target_link_libraries(chibi_task_scheduler PRIVATE c++)
    elseif(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_link_libraries(chibi_task_scheduler PRIVATE stdc++)
    endif()
    target_link_libraries(chibi_task_pool_runtime PRIVATE chibi_task_scheduler)
    target_include_directories(chibi_task_pool_runtime PRIVATE "${enkits_root}/src")
    target_compile_definitions(chibi_task_pool_runtime PRIVATE CHIBI_HAS_ENKITS=1)
    if(CHIBI_WEB_THREADS)
        target_compile_definitions(chibi_task_pool_runtime PRIVATE CHIBI_WEB_WORKERS=${CHIBI_WEB_WORKERS})
    endif()
    install(FILES "${enkits_root}/License.txt" DESTINATION share/chibi/licenses/enkits)
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
