cmake_minimum_required(VERSION 3.24)
project(chibi_ctl LANGUAGES C CXX)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT TARGET Chibi::project_preparation)
    find_package(Chibi CONFIG REQUIRED)
endif()

set(packages asset_authoring asset_json asset_text authoring behavior_runtime
    component_presentation ecs execution execution_contract field_catalog
    host_native_default image_preparation json
    model_authoring model_import model_runtime native_capabilities native_types platform project_document
    project_preparation project_compilation project_format prototype_document reference_index render
    scenario_document scene_document shader_preparation source_catalog source_presentation
    studio_capabilities support text_search text_source types ui ui_authoring
    ui_document ui_runtime visual_catalog)
foreach(package IN LISTS packages)
    if(NOT TARGET Chibi::${package})
        message(FATAL_ERROR "chibi_ctl requires installed Chibi::${package}")
    endif()
endforeach()
if(NOT Chibi_RESOURCE_DIR)
    message(FATAL_ERROR "The installed SDK must export Chibi_RESOURCE_DIR")
endif()
get_filename_component(tool_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(automation_units controller field_preview material_bake navigation content_hash ui_inspect
    execution_policy render_preview scene_preview)
set(automation_headers controller field_preview material_bake navigation content_hash ui_inspect
    execution_policy render_preview gpu_capture)
set(private_headers "${CMAKE_CURRENT_BINARY_DIR}/private")
foreach(header IN LISTS automation_headers)
    configure_file("${tool_root}/src/automation/${header}.h"
        "${private_headers}/automation/${header}.h" COPYONLY)
endforeach()
# Profiling macros belong to this controller build, with no engine source root.
foreach(header tracy web_trace)
    configure_file("${tool_root}/src/diagnostics/${header}.h"
        "${private_headers}/diagnostics/${header}.h" COPYONLY)
endforeach()
configure_file("${tool_root}/src/support/execution_policy.h"
    "${private_headers}/support/execution_policy.h" COPYONLY)
add_library(chibi_automation STATIC)
foreach(unit IN LISTS automation_units)
    target_sources(chibi_automation PRIVATE "${tool_root}/src/automation/${unit}.c")
endforeach()
if(APPLE)
    enable_language(OBJC)
    target_sources(chibi_automation PRIVATE "${tool_root}/src/automation/gpu_capture.m")
    target_compile_options(chibi_automation PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-Wno-elaborated-enum-base>)
    target_link_libraries(chibi_automation PRIVATE "-framework Metal" "-framework Foundation")
else()
    target_sources(chibi_automation PRIVATE "${tool_root}/src/automation/gpu_capture.c")
endif()
target_include_directories(chibi_automation PRIVATE "${private_headers}")
foreach(package IN LISTS packages)
    target_link_libraries(chibi_automation PRIVATE Chibi::${package})
endforeach()
add_executable(chibi_ctl main.c prepare.c registry.c)
target_compile_definitions(chibi_ctl PRIVATE CHIBI_CTL_STANDALONE=1)
target_include_directories(chibi_ctl PRIVATE "${private_headers}")
target_link_libraries(chibi_ctl PRIVATE chibi_automation)
# In the engine tree the SDK's function adds the manifest; installed SDKs
# ship its resource script.
if(COMMAND chibi_windows_manifest)
    chibi_windows_manifest(chibi_ctl)
elseif(WIN32)
    enable_language(RC)
    target_sources(chibi_ctl PRIVATE "${Chibi_RESOURCE_DIR}/windows/chibi.rc")
    set_source_files_properties("${Chibi_RESOURCE_DIR}/windows/chibi.rc"
        PROPERTIES OBJECT_DEPENDS "${Chibi_RESOURCE_DIR}/windows/chibi.manifest")
endif()
foreach(package IN LISTS packages)
    target_link_libraries(chibi_ctl PRIVATE Chibi::${package})
endforeach()
set_target_properties(chibi_ctl PROPERTIES LINKER_LANGUAGE CXX
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_automation PRIVATE -Wall -Wextra -Werror)
    target_compile_options(chibi_ctl PRIVATE -Wall -Wextra -Werror)
endif()
# The process resolves its own installed engine metadata/shaders. Authored
# project content remains supplied by --project-root.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/chibi")
foreach(resource engine.json shaders licenses)
    if(EXISTS "${Chibi_RESOURCE_DIR}/${resource}")
        file(COPY "${Chibi_RESOURCE_DIR}/${resource}" DESTINATION "${CMAKE_BINARY_DIR}/share/chibi")
        if(IS_DIRECTORY "${Chibi_RESOURCE_DIR}/${resource}")
            install(DIRECTORY "${Chibi_RESOURCE_DIR}/${resource}" DESTINATION share/chibi)
        else()
            install(FILES "${Chibi_RESOURCE_DIR}/${resource}" DESTINATION share/chibi)
        endif()
    endif()
endforeach()
install(TARGETS chibi_ctl RUNTIME DESTINATION bin)
