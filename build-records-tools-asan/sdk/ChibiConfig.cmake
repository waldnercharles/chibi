
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ChibiConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################
# The SDK links the C runtime statically on Windows; consumers must match.
if(WIN32)
    if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND
       NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded")
        message(FATAL_ERROR "The Chibi SDK links the MultiThreaded C runtime, "
            "not ${CMAKE_MSVC_RUNTIME_LIBRARY}")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set(Chibi_WEB_THREADS OFF)
set(Chibi_WEB_WORKERS 4)
include("${CMAKE_CURRENT_LIST_DIR}/ChibiVendorDependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ChibiTargets.cmake")
set(Chibi_RESOURCE_DIR "${PACKAGE_PREFIX_DIR}/share/chibi")
set(Chibi_CORE_SOURCE_DIR "${Chibi_RESOURCE_DIR}/core")
foreach(component support types ecs events execution_contract execution behavior_runtime
    compiler_host asset_compiler_host asset_values value_document capabilities native_capabilities native_prepared core_capabilities array_capabilities field_capabilities state_capabilities selection_capabilities set_capabilities comparison_capabilities conversion_capabilities text_capabilities fmt host_native_catalog scene_runtime scheduling fields field_catalog tasks math native_types native_bindings tilemap_runtime spatial_runtime entity_runtime audio_runtime url_runtime vector_capabilities math_capabilities arithmetic_capabilities particle_runtime save_runtime ui_runtime input_action input_runtime host_native_default studio_catalog visibility_runtime engine player material_runtime physics_runtime physics3d_runtime declared_components task_pool_runtime box2d box3d task_scheduler
    shader_runtime model_runtime mesh_runtime model_bake platform ui render ui_documents ui_document_values text_search image_catalog image_preparation builtin_fonts asset_json studio_graph graph_catalog
    scene_authoring asset_authoring model_authoring model_import scenario_document source_catalog source_presentation visual_catalog shader_preparation project_compilation project_format project_preparation graph_compilation graph_model graph_authoring language_syntax language compiler compiler_syntax compiler_json authoring studio_capabilities component_presentation input_map project_document prototype_document scene_document reference_index asset_text asset_kdl syntax_runtime json text_source ui_document ui_authoring ui_syntax)
    set(Chibi_${component}_FOUND FALSE)
    if(TARGET Chibi::${component})
        set(Chibi_${component}_FOUND TRUE)
    endif()
endforeach()
foreach(component compiler_host asset_compiler_host)
    if(component IN_LIST Chibi_FIND_COMPONENTS AND Chibi_FIND_REQUIRED_${component}
       AND NOT Chibi_${component}_FOUND)
        set(Chibi_NOT_FOUND_MESSAGE "Required Chibi component '${component}' is unavailable in this SDK configuration")
    endif()
endforeach()
check_required_components(Chibi)
