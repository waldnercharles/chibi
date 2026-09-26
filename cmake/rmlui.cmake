# Keep the retained UI and its font rasterizer inside the application module.
# Function scope avoids changing BUILD_SHARED_LIBS for Cute's resident library.
function(chibi_rmlui_dependencies)
    set(BUILD_SHARED_LIBS OFF)
    foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
        set(FT_DISABLE_${feature} ON CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG 0a0221a1347e2f1e07c395263540026e9a0aa7c7 # 2.14.3
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(freetype)
    add_library(Freetype::Freetype ALIAS freetype)

    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
    FetchContent_Declare(rmlui
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG ba95ffe8bfb6370efb2cdcca927eaad4710c5413 # 6.3
        UPDATE_DISCONNECTED TRUE)
    FetchContent_GetProperties(rmlui)
    if(NOT rmlui_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(rmlui)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()
    # Web behavior RmlUi lacks: CSS color-mix(), so theme roles derive from a
    # few tokens at runtime, and HTML's radio groups, which need a name.
    set(PATCH_SOURCE "${rmlui_SOURCE_DIR}")
    foreach(patch color-mix radio-groups)
        set(PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/rmlui-${patch}.patch")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PATCH_FILE}")
        include(cmake/apply_patch.cmake)
    endforeach()
    add_subdirectory("${rmlui_SOURCE_DIR}" "${rmlui_BINARY_DIR}")
    foreach(target freetype rmlui_core rmlui_debugger)
        set_target_properties(${target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
    endforeach()
endfunction()

chibi_rmlui_dependencies()

# The shared retained UI layer: the RmlUi binding, its Cute backend and the
# embedded faces. Game UI and the editor kit both build on it.
add_library(chibi_ui STATIC
    src/ui/rml.cpp
    src/ui/rml_backend.c
    # The embedded faces, loaded by the editor kit and game UI alike.
    ${GEN_NERD_FONT} ${GEN_UI_FONT} ${GEN_UI_BOLD_FONT}
    ${GEN_UI_MEDIUM_FONT} ${GEN_UI_SEMIBOLD_FONT} ${GEN_UI_NUMERIC_FONT})
add_dependencies(chibi_ui generate_editor_nerd_symbols_font generate_editor_ui_font generate_editor_ui_bold_font
    generate_editor_ui_medium_font generate_editor_ui_semibold_font generate_editor_ui_numeric_font)
target_compile_features(chibi_ui PRIVATE cxx_std_17)
target_include_directories(chibi_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(chibi_ui SYSTEM PRIVATE ${cute_framework_SOURCE_DIR}/include)
target_link_libraries(chibi_ui PRIVATE RmlUi::Core cute core)
set_target_properties(chibi_ui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

add_library(chibi_rmlui STATIC
    src/editor/rmlui/kit.c
    src/editor/rmlui/kit_chart.c
    src/editor/rmlui/kit_command.c
    src/editor/rmlui/kit_controls.c
    src/editor/rmlui/kit_drag.c
    src/editor/rmlui/kit_list.c
    src/editor/rmlui/kit_motion.c
    src/editor/rmlui/kit_number.c
    src/editor/rmlui/kit_popup.c
    src/editor/rmlui/kit_text.c
    src/editor/rmlui/kit_toast.c
    src/editor/rmlui/kit_wires.c
    # kit_wires.c draws through it, so it links with the kit.
    src/editor/ui/kit_wire_shape.c)
target_include_directories(chibi_rmlui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(chibi_rmlui SYSTEM PRIVATE ${cute_framework_SOURCE_DIR}/include)
target_compile_definitions(chibi_rmlui PRIVATE
    CHIBI_RMLUI_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/editor/rmlui/resources")
# core supplies arrange_path to kit_wire_shape.c; listing it here keeps it
# after this library on single-pass link lines.
target_link_libraries(chibi_rmlui PUBLIC chibi_ui PRIVATE cute core)
set_target_properties(chibi_rmlui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
