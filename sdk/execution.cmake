set(CHIBI_EXECUTION_SOURCES
    artifact.c backend.c borrow.c c.c call.c component.c ecs_reader.c function.c function_builder.c fusion.c host.c inputs.c integer_report.c intrinsics.c query.c map_intrinsics.c
    jit.c jit_component.c jit_host_call.c jit_integer.c jit_memory.c jit_registers.c
    jit_suspend.c jit_system.c jit_watch.c log.c machine.c module.c module_effects.c opt.c parallel.c
    native.c preparation_budget.c prepared.c prepared_function.c prepared_record.c program.c program_inspection.c program_activation.c
    program_observers.c program_bindings.c program_commands.c program_locals.c program_storage.c program_preflight.c
    program_tasks.c range_opt.c reads.c request.c runner.c scalar_lowering_plan.c scalarize.c source_debug.c state.c storage.c suspend.c time.c vm.c
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
    execution/delay.h execution/task.h execution/inspection.h execution/integer_report.h execution/prepared.h execution/ecs_reader.h
    execution/component_write.h execution/native.h)
target_include_directories(chibi_execution PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/execution"
    "${CMAKE_CURRENT_BINARY_DIR}/private/types")
target_link_libraries(chibi_execution PUBLIC chibi_types chibi_execution_contract chibi_ecs chibi_tasks)

# The prepared reader and writer share a translation unit. Let the compiler
# host's section-GC link discard the unused public writer entry point.
if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32) AND
   CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
    target_compile_options(chibi_execution PRIVATE -ffunction-sections -fdata-sections)
endif()

option(CHIBI_ENABLE_SJLIT "Build the native execution backend" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(CHIBI_ENABLE_SJLIT OFF)
    target_link_options(chibi_execution INTERFACE -sALLOW_TABLE_GROWTH=1)
endif()
target_compile_definitions(chibi_execution PRIVATE CHIBI_HAVE_SJLIT=$<BOOL:${CHIBI_ENABLE_SJLIT}>)
if(CHIBI_ENABLE_SJLIT)
    if(CHIBI_SDK_COMPILER_HOST_ONLY)
        set(sljit_root "${CHIBI_THIRD_PARTY_DIR}/source/sljit/sljit_src")
        add_library(chibi_execution_sljit STATIC "${sljit_root}/sljitLir.c")
        set_target_properties(chibi_execution_sljit PROPERTIES EXPORT_NAME execution_sljit)
        target_compile_definitions(chibi_execution_sljit PRIVATE SLJIT_WX_EXECUTABLE_ALLOCATOR=1)
        target_compile_options(chibi_execution_sljit PRIVATE -w)
        target_include_directories(chibi_execution PRIVATE "${sljit_root}")
        install(TARGETS chibi_execution_sljit EXPORT ChibiTargets ARCHIVE DESTINATION lib)
        install(FILES "${CHIBI_THIRD_PARTY_DIR}/source/sljit/LICENSE"
            DESTINATION share/chibi/licenses/sljit)
    else()
        chibi_sdk_vendor(chibi_execution_sljit execution_sljit sljit)
        install(TARGETS chibi_execution_sljit EXPORT ChibiTargets)
    endif()
    target_link_libraries(chibi_execution PRIVATE chibi_execution_sljit)
endif()
