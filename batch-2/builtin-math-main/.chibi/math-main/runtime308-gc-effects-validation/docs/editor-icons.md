# Editor icons

The editor uses the embedded Nerd Font Symbols font for UI icons. Kit and Studio
controls take named `KIT_ICON_*` values from
[kit_icons.h](../src/editor/ui/kit_icons.h), not unexplained numeric codepoints.

For a new icon, search the [Nerd Fonts cheat sheet](https://www.nerdfonts.com/cheat-sheet),
add its published name and codepoint to `src/editor/ui/kit_icons.h`, and use that
constant at the call site. The `nf-` name is intentionally kept in the comment so
an AI agent can find an icon by family, name, or codepoint with `rg`.

`src/editor/rmlui/backend.c` loads the symbol font as the RmlUi `Symbols` face.
Material Design icons use the supplementary private-use plane, such as
`nf-md-play` (U+F040A).
