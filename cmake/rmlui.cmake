if(NOT CHIBI_DEPENDENCY_PACKAGE)
    include(cmake/rmlui_dependencies.cmake)
endif()

# The shared retained UI layer: the RmlUi binding, its Cute backend and the
# embedded faces. Game UI and the editor kit both build on it.
add_library(chibi_ui STATIC
    src/ui/rml.cpp
    src/ui/json_items.c
    src/ui/rml_backend.c
    src/ui/rml_gpu.c
    # Documents shown in another document's element, laid out apart.
    src/ui/iframe.c
    # Motion and the native elements authored UI uses: <number-flow>,
    # <morph-text> and presence for data-if.
    src/ui/motion.c
    src/ui/text_motion.c
    # Native controls (workstream F2): <number-field>, <color-field>,
    # <splitter> and <live-text>; F3's <choice-list>.
    src/ui/controls/controls.c
    src/ui/controls/choice_list.c
    src/ui/controls/color_field.c
    src/ui/controls/focus.c
    src/ui/controls/line_text.c
    src/ui/controls/live_text.c
    src/ui/controls/number_field.c
    src/ui/controls/splitter.c
    # Popovers, menus and menubars (workstream F3).
    src/ui/popover.c
    # Lists and drag (workstream F4): element-level drag and drop and
    # virtualized list windows for every context.
    src/ui/drag.c
    src/ui/list.c
    # :focus-within, which RmlUi lacks (workstream P3).
    src/ui/focus_within.c
    # The embedded faces, loaded by the editor kit and game UI alike.
    ${GEN_NERD_FONT} ${GEN_UI_FONT} ${GEN_UI_BOLD_FONT}
    ${GEN_UI_MEDIUM_FONT} ${GEN_UI_SEMIBOLD_FONT} ${GEN_UI_NUMERIC_FONT} ${GEN_STUDIO_FONTS}
    ${GEN_NOTO_SANS_FONT} ${GEN_NOTO_SANS_SEMIBOLD_FONT} ${GEN_NOTO_SANS_BOLD_FONT}
    ${GEN_INCONSOLATA_FONT})
add_dependencies(chibi_ui generate_editor_nerd_symbols_font generate_editor_ui_font generate_editor_ui_bold_font
    generate_editor_ui_medium_font generate_editor_ui_semibold_font generate_editor_ui_numeric_font ${STUDIO_FONT_TARGETS}
    generate_editor_noto_sans_font generate_editor_noto_sans_semibold_font
    generate_editor_noto_sans_bold_font generate_editor_inconsolata_font)
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
    # The UI Inspector's overlay and element paths.
    src/editor/rmlui/kit_inspect.c
    # Native slots: kit content inside authored markup.
    src/editor/rmlui/kit_slot.c
    # kit_wires.c draws through it, so it links with the kit.
    src/editor/ui/kit_wire_shape.c)
target_include_directories(chibi_rmlui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(chibi_rmlui SYSTEM PRIVATE ${cute_framework_SOURCE_DIR}/include)
# core supplies arrange_path to kit_wire_shape.c; listing it here keeps it
# after this library on single-pass link lines.
target_link_libraries(chibi_rmlui PUBLIC chibi_ui PRIVATE cute core)
set_target_properties(chibi_rmlui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
