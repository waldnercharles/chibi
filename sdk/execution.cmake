set(CHIBI_EXECUTION_SOURCES
    artifact.c authority.c backend.c borrow.c c.c call.c component.c ecs_reader.c function.c function_builder.c fusion.c host.c inputs.c intrinsics.c map_intrinsics.c
    jit.c jit_component.c jit_host_call.c jit_integer.c jit_memory.c jit_registers.c
    jit_suspend.c jit_system.c jit_watch.c log.c machine.c module.c module_effects.c opt.c parallel.c
    preparation_budget.c prepared.c prepared_function.c prepared_record.c prepared_producer.c program.c program_inspection.c program_activation.c
    program_observers.c program_bindings.c program_commands.c program_locals.c program_storage.c program_preflight.c
    program_tasks.c range_opt.c reads.c request.c row_builder.c runner.c scalarize.c source_debug.c state.c storage.c suspend.c time.c vm.c
    update_jit.c wasm_emit.c wasm_jit.c)
list(TRANSFORM CHIBI_EXECUTION_SOURCES PREPEND "${CHIBI_SDK_ROOT}/src/execution/")

# Private implementation headers are available only to this package. A graph
# include here fails even when this SDK is configured inside the product build.
file(GLOB execution_headers CONFIGURE_DEPENDS "${CHIBI_SDK_ROOT}/src/execution/*.h")
file(GLOB_RECURSE previous_execution_headers LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_BINARY_DIR}/private/execution/*.h")
foreach(header IN LISTS previous_execution_headers)
    get_filename_component(name "${header}" NAME)
    if(NOT header STREQUAL "${CMAKE_CURRENT_BINARY_DIR}/private/execution/execution/${name}" OR
       NOT EXISTS "${CHIBI_SDK_ROOT}/src/execution/${name}")
        file(REMOVE "${header}")
    endif()
endforeach()
foreach(header IN LISTS execution_headers)
    get_filename_component(name "${header}" NAME)
    configure_file("${header}" "${CMAKE_CURRENT_BINARY_DIR}/private/execution/execution/${name}" COPYONLY)
endforeach()
add_library(chibi_execution STATIC ${CHIBI_EXECUTION_SOURCES})
add_library(Chibi::execution ALIAS chibi_execution)
set_target_properties(chibi_execution PROPERTIES EXPORT_NAME execution)
chibi_sdk_headers(chibi_execution PUBLIC execution/function_builder.h execution/update.h
    execution/module.h execution/module_builder.h execution/intrinsics.h execution/map_intrinsics.h
    execution/delay.h execution/task.h execution/inspection.h execution/prepared.h execution/ecs_reader.h
    execution/component_write.h execution/authority.h execution/prepared_producer.h)
target_include_directories(chibi_execution PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/execution"
    "${CMAKE_CURRENT_BINARY_DIR}/private/types")
target_link_libraries(chibi_execution PUBLIC chibi_types chibi_execution_contract chibi_ecs chibi_tasks)

option(CHIBI_ENABLE_SJLIT "Build the native execution backend" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(CHIBI_ENABLE_SJLIT OFF)
    target_link_options(chibi_execution INTERFACE -sALLOW_TABLE_GROWTH=1)
endif()
target_compile_definitions(chibi_execution PRIVATE CHIBI_HAVE_SJLIT=$<BOOL:${CHIBI_ENABLE_SJLIT}>)
if(CHIBI_ENABLE_SJLIT)
    if(SLJIT_SOURCE_DIR)
        set(execution_sljit_source "${SLJIT_SOURCE_DIR}")
    elseif(CHIBI_SJLIT_SOURCE_DIR)
        set(execution_sljit_source "${CHIBI_SJLIT_SOURCE_DIR}")
    else()
        FetchContent_Declare(chibi_execution_sljit_source
            GIT_REPOSITORY https://github.com/zherczeg/sljit.git
            GIT_TAG a0020dd49a21020434ff838fc308626c48cd8f05
            SOURCE_SUBDIR chibi-sources-only)
        FetchContent_MakeAvailable(chibi_execution_sljit_source)
        set(execution_sljit_source "${chibi_execution_sljit_source_SOURCE_DIR}")
    endif()
    # Keep dependency caches read-only. The copied entry point resolves its
    # ARM64 backend locally; other includes still use the original source.
    set(sljit_patch "${CHIBI_SDK_ROOT}/cmake/patches/sljit-arm64-immediate-negation.patch")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sljit_patch}")
    set(sljit_patched_root "${CMAKE_CURRENT_BINARY_DIR}/private/sljit")
    foreach(source sljitLir.c sljitNativeARM_64.c)
        configure_file("${execution_sljit_source}/sljit_src/${source}"
            "${sljit_patched_root}/${source}" COPYONLY)
    endforeach()
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
            "--directory=${sljit_patched_root}" "${sljit_patch}"
        RESULT_VARIABLE sljit_patch_result
        ERROR_VARIABLE sljit_patch_error)
    if(NOT sljit_patch_result EQUAL 0)
        message(FATAL_ERROR "Could not patch SLJIT ARM64 immediates: ${sljit_patch_error}")
    endif()
    add_library(chibi_execution_sljit STATIC "${sljit_patched_root}/sljitLir.c")
    set_target_properties(chibi_execution_sljit PROPERTIES EXPORT_NAME execution_sljit)
    target_include_directories(chibi_execution_sljit PRIVATE "${execution_sljit_source}/sljit_src")
    target_compile_definitions(chibi_execution_sljit PRIVATE SLJIT_WX_EXECUTABLE_ALLOCATOR=1)
    target_compile_options(chibi_execution_sljit PRIVATE -w)
    target_include_directories(chibi_execution PRIVATE "${execution_sljit_source}/sljit_src")
    target_link_libraries(chibi_execution PRIVATE chibi_execution_sljit)
    install(TARGETS chibi_execution_sljit EXPORT ChibiTargets ARCHIVE DESTINATION lib)
    install(FILES "${execution_sljit_source}/LICENSE" DESTINATION share/chibi/licenses/sljit)
endif()
