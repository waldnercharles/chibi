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
    FetchContent_MakeAvailable(rmlui)
    foreach(target freetype rmlui_core rmlui_debugger)
        set_target_properties(${target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
    endforeach()
endfunction()

chibi_rmlui_dependencies()

add_library(chibi_rmlui STATIC
    src/editor/rmlui/rml.cpp
    src/editor/rmlui/backend.cpp
    src/editor/rmlui/kit.cpp
    src/editor/rmlui/kit_chart.cpp
    src/editor/rmlui/kit_command.cpp
    src/editor/rmlui/kit_controls.cpp
    src/editor/rmlui/kit_drag.cpp
    src/editor/rmlui/kit_list.cpp
    src/editor/rmlui/kit_motion.cpp
    src/editor/rmlui/kit_number.cpp
    src/editor/rmlui/kit_popup.cpp
    src/editor/rmlui/kit_text.cpp
    src/editor/rmlui/kit_toast.cpp
    src/editor/rmlui/kit_wires.cpp
    # kit_wires.cpp draws through it, so it links with the kit.
    src/editor/ui/kit_wire_shape.c
    src/editor/rmlui/menu.cpp
    src/editor/rmlui/toolbar.cpp
    # The editor's embedded faces; the ImGui panes read them through editor,
    # which links this library, so single-pass linkers resolve them too.
    ${GEN_NERD_FONT} ${GEN_UI_FONT} ${GEN_UI_BOLD_FONT}
    ${GEN_UI_MEDIUM_FONT} ${GEN_UI_SEMIBOLD_FONT} ${GEN_UI_NUMERIC_FONT})
add_dependencies(chibi_rmlui generate_editor_nerd_symbols_font generate_editor_ui_font generate_editor_ui_bold_font
    generate_editor_ui_medium_font generate_editor_ui_semibold_font generate_editor_ui_numeric_font)
target_compile_features(chibi_rmlui PRIVATE cxx_std_17)
target_include_directories(chibi_rmlui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(chibi_rmlui SYSTEM PRIVATE ${cute_framework_SOURCE_DIR}/include)
target_compile_definitions(chibi_rmlui PRIVATE
    CHIBI_RMLUI_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/editor/rmlui/resources")
# core supplies arrange_path to kit_wire_shape.c; listing it here keeps it
# after this library on single-pass link lines.
target_link_libraries(chibi_rmlui PRIVATE RmlUi::Core cute core)
set_target_properties(chibi_rmlui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
