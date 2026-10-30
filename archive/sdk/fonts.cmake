# Immutable font bytes are available to offline preparation without a UI backend.
add_library(chibi_builtin_fonts STATIC "${CHIBI_SDK_ROOT}/src/runtime/ui/builtin_fonts.c")
add_library(Chibi::builtin_fonts ALIAS chibi_builtin_fonts)
set_target_properties(chibi_builtin_fonts PROPERTIES EXPORT_NAME builtin_fonts)
chibi_sdk_headers(chibi_builtin_fonts PUBLIC runtime/ui/builtin_fonts.h)
target_link_libraries(chibi_builtin_fonts PUBLIC chibi_ui_documents)
function(chibi_builtin_font path symbol)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/fonts/${symbol}.c")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${CHIBI_SDK_ROOT}/third_party/fonts/${path}"
            "-DOUTPUT=${output}" "-DSYMBOL=${symbol}"
            -P "${CHIBI_SDK_ROOT}/cmake/embed_binary.cmake"
        DEPENDS "${CHIBI_SDK_ROOT}/third_party/fonts/${path}"
            "${CHIBI_SDK_ROOT}/cmake/embed_binary.cmake"
            "${CHIBI_SDK_ROOT}/cmake/generated_file.cmake" VERBATIM)
    target_sources(chibi_builtin_fonts PRIVATE "${output}")
endfunction()
chibi_builtin_font(nerd-fonts-symbols-only/SymbolsNerdFont-Regular.ttf editor_nerd_symbols_font)
chibi_builtin_font(inter/Inter.ttf editor_ui_font)
chibi_builtin_font(inter/Inter-Bold.ttf editor_ui_bold_font)
chibi_builtin_font(inter/Inter-Medium.ttf editor_ui_medium_font)
chibi_builtin_font(inter/Inter-SemiBold.ttf editor_ui_semibold_font)
chibi_builtin_font(inter/Inter-Numeric.ttf editor_ui_numeric_font)
foreach(face Regular Medium SemiBold Bold Numeric)
    string(TOLOWER "${face}" key)
    chibi_builtin_font("geist/Geist-${face}.ttf" "studio_${key}_font")
endforeach()
chibi_builtin_font(noto-sans/NotoSans-Regular.ttf editor_noto_sans_font)
chibi_builtin_font(noto-sans/NotoSans-SemiBold.ttf editor_noto_sans_semibold_font)
chibi_builtin_font(noto-sans/NotoSans-Bold.ttf editor_noto_sans_bold_font)
chibi_builtin_font(inconsolata/Inconsolata-Regular.ttf editor_inconsolata_font)
chibi_builtin_font(jetbrains-mono/JetBrainsMono-Regular.ttf editor_code_font)

list(APPEND CHIBI_SDK_TARGETS chibi_builtin_fonts)
install(DIRECTORY "${CHIBI_SDK_ROOT}/third_party/fonts/" DESTINATION share/chibi/licenses/fonts
    FILES_MATCHING PATTERN "*LICENSE*" PATTERN "*OFL*" PATTERN "*COPYING*")
# These constituent notices use lowercase names, and NOTICES.md records their
# attribution and provenance. Preserve that relative layout without font bytes.
install(FILES "${CHIBI_SDK_ROOT}/third_party/fonts/nerd-fonts-symbols-only/NOTICES.md"
    DESTINATION share/chibi/licenses/fonts/nerd-fonts-symbols-only)
install(DIRECTORY "${CHIBI_SDK_ROOT}/third_party/fonts/nerd-fonts-symbols-only/licenses"
    DESTINATION share/chibi/licenses/fonts/nerd-fonts-symbols-only)
