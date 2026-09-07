# Icon audit

Commit 90fc6e6f, inspected working tree 2026-09-06. Source remained unchanged. Inventory covers every explicit four-digit private-use codepoint and escaped UTF-8 private-use glyph in app/, editor/, engine/ C headers and sources. Mathematical text and ImGui-drawn chevrons are not font icons. Line references reflect this inspection and may drift during concurrent work.

The bundled font is third_party/fonts/nerd-fonts-symbols-only/SymbolsNerdFont-Regular.ttf, documented as Nerd Fonts v3.5.0. Codepoint names were read from the version-pinned [Nerd Fonts glyph mapping](https://raw.githubusercontent.com/ryanoasis/nerd-fonts/v3.5.0/glyphnames.json). Presence was independently checked in that local TTF's format-12 cmap using Python struct. No font installation or dependency changes were needed.

## Sidebar mapping

| Action | Exact Nerd Font name | Codepoint | UTF-8 C string | Bundled font |
| --- | --- | --- | --- | --- |
| Hide Navigator | nf-cod-layout_sidebar_left_off | U+EC02 | `"\xee\xb0\x82"` | present |
| Show Navigator | nf-cod-layout_sidebar_left | U+EBF3 | `"\xee\xaf\xb3"` | present |
| Hide Inspector | nf-cod-layout_sidebar_right_off | U+EC00 | `"\xee\xb0\x80"` | present |
| Show Inspector | nf-cod-layout_sidebar_right | U+EBF4 | `"\xee\xaf\xb4"` | present |

editor/editor.c:97 currently merges only F000–F385 and F400–F533. The font contains all four requested Codicons, but those ranges exclude them. Add sorted singleton ranges EBF3–EBF4, EC00–EC00, EC02–EC02 before the existing ranges. This is narrower than loading the entire Codicon set. Update ui_sidebar_heading at editor/ui_shell.c:1718 and the collapsed rail at :1820. Use action semantics above consistently in tooltip and icon. Current chevrons F104/F105 imply direction without identifying the panel.

## Semantic mapping review

Keep shared asset icons stable across Files, Inspector identity and Document outline: folder F07B, prototype F1B2, behavior F126, Scene F06E, system F085, gameplay/data F1C0, generic file F15B. Keep transport Play F04B, Pause F04C, Stop F04D, Step F051 and Restart F01E identical across header, graph and Scene controls. Keep Fit F065 and camera reset F05B distinct. Existing Revert F0E2 and Restart F01E are distinct directions and actions. Attachment remove F127 means unlink. The original component delete glyph F2ED actually maps to the Joget brand in this bundled font, not a trash can. Corrected the helper to F1F8, the verified bundled fa-trash glyph within the existing merged atlas range. Preserve the semantic distinction between unlink and delete. Graph Branch/function/event/type icons are semantic node symbols, not interchangeable navigation chrome.

Verification must bake the merged font and assert ImFontBaked_FindGlyphNoFallback for all new values, then inspect both sidebars at normal and high DPI. Source cmap presence alone does not prove atlas inclusion or rendered optical alignment. ui_draw_centered_icon already centers ink bounds rather than font advance, which should be preserved.

## Complete explicit source inventory

| Codepoint | Versioned Nerd Font aliases | Local cmap | Source occurrences |
| --- | --- | --- | --- |
| U+F000 | fa-glass, fa-martini_glass_empty | yes | editor/editor.c:97 |
| U+F002 | fa-magnifying_glass, fa-search | yes | editor/ui_shell.c:49 |
| U+F00D | fa-close, fa-remove, fa-times, fa-xmark | yes | editor/ui_shell.c:998, editor/editor.c:92 |
| U+F013 | fa-cog, fa-gear | yes | editor/editor.c:94, editor/editor.c:484 |
| U+F01E | fa-arrow_rotate_right, fa-repeat | yes | editor/ui_shell.c:1496, editor/editor.c:5989, engine/viewport.c:286 |
| U+F040 | fa-pencil | yes | editor/editor.c:471 |
| U+F04B | fa-play | yes | editor/ui_shell.c:1474, editor/editor.c:5989, engine/viewport.c:283 |
| U+F04C | fa-pause | yes | editor/ui_shell.c:1474, editor/editor.c:5989, engine/viewport.c:283 |
| U+F04D | fa-stop | yes | editor/ui_shell.c:1481, editor/editor.c:5989, engine/viewport.c:284 |
| U+F051 | fa-forward_step, fa-step_forward | yes | editor/ui_shell.c:1488, editor/editor.c:5989, engine/viewport.c:285 |
| U+F05B | fa-crosshairs | yes | engine/viewport.c:288 |
| U+F060 | fa-arrow_left | yes | editor/ui_shell.c:1727 |
| U+F061 | fa-arrow_right | yes | editor/ui_shell.c:1735 |
| U+F065 | fa-expand | yes | editor/editor.c:6099, engine/viewport.c:287 |
| U+F067 | fa-plus | yes | app/scene_authoring.c:6138, editor/editor.c:93 |
| U+F06E | fa-eye | yes | editor/ui_shell.c:702, editor/editor.c:468 |
| U+F071 | fa-exclamation_triangle, fa-triangle_exclamation, fa-warning | yes | editor/editor.c:91, editor/editor.c:98 |
| U+F07B | fa-folder | yes | editor/ui_shell.c:699 |
| U+F085 | fa-cogs, fa-gears | yes | editor/ui_shell.c:703 |
| U+F0E2 | fa-arrow_rotate_left, fa-undo | yes | app/scene_authoring.c:5541 |
| U+F0E7 | fa-bolt, fa-flash | yes | editor/editor.c:475 |
| U+F0E8 | fa-sitemap | yes | editor/editor.c:448 |
| U+F104 | fa-angle_left | yes | editor/ui_shell.c:1718, editor/ui_shell.c:1820 |
| U+F105 | fa-angle_right | yes | editor/ui_shell.c:1718, editor/ui_shell.c:1820 |
| U+F107 | fa-angle_down | yes | editor/ui_shell.c:746, editor/ui_shell.c:751 |
| U+F121 | fa-code | yes | editor/editor.c:481 |
| U+F126 | fa-code_branch, fa-code_fork | yes | editor/ui_shell.c:701, editor/editor.c:465, editor/editor.c:477 |
| U+F127 | fa-chain_broken, fa-link_slash, fa-unlink | yes | app/scene_authoring.c:5849 |
| U+F141 | fa-ellipsis, fa-ellipsis_h | yes | editor/editor.c:6041, engine/viewport.c:335 |
| U+F15B | fa-file | yes | editor/ui_shell.c:705 |
| U+F188 | fa-bug | yes | editor/editor.c:6113 |
| U+F1B2 | fa-cube | yes | app/scene_authoring.c:5244, editor/ui_shell.c:700, editor/editor.c:99 |
| U+F1C0 | fa-database | yes | editor/ui_shell.c:704, editor/editor.c:479 |
| U+F1D8 | fa-paper_plane, fa-send | yes | editor/editor.c:445 |
| U+F1F8 | fa-trash | yes | editor/ui_shell.c component-header removal, corrected from F2ED |
| U+F385 | linux-cachyos | yes | editor/editor.c:97 |
| U+F400 | oct-light_bulb | yes | editor/editor.c:97 |
| U+F533 | oct-bookmark_slash | yes | editor/editor.c:97 |

Final semantic sweep: F385, F400 and F533 occur as font range bounds, not rendered action icons. The other inventory entries match their named actions or documented asset/node categories; no further semantic replacement is justified.
