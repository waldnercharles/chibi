# Editor icons

The editor uses the embedded Nerd Font Symbols font for UI icons. Icon controls
take named `editor_icon` values from [editor/icons.h](../editor/icons.h),
not unexplained numeric codepoints.

For a new icon, search the [Nerd Fonts cheat sheet](https://www.nerdfonts.com/cheat-sheet),
add its published name and codepoint to `editor/icons.h`, and use that constant
at the call site. The `nf-` name is intentionally kept in the comment so an AI
agent can find an icon by family, name, or codepoint with `rg`.

The default icon font is loaded and merged in `editor/editor.c` by
`ed_load_icon_font()`. Font Awesome and Material Design icons are included in
the embedded symbol font ranges. Material Design icons use the supplementary
private-use plane; the editor enables Dear ImGui's 32-bit `ImWchar` mode so
`nf-md-arrow_top_right` (U+F005C) is not truncated.
