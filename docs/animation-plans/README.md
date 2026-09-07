# Animation plans

Audit baseline 90fc6e6f, working tree 2026-09-06. Source remains unchanged by this audit. The user has authorized implementation; TODO means pending execution, not pending permission.

| Number | Title | Severity | Status |
| --- | --- | --- | --- |
| 001 | Make keyboard disclosure immediate | MEDIUM | Implemented and verified |
| 002 | Shorten hover and add restrained pointer press | LOW | Implemented and verified |
| 003 | Fade occasional pointer popups | LOW | Implemented and verified |
| 004 | Shorten connection error fade | LOW | Implemented and verified |

Recommended order 001, 002, 004, 003. Plans 001/002/003 share ui_shell.c and should execute serially under one owner. Plan 004 touches editor.c and must coordinate with the graph owner. No plan requires another plan's implementation. Read audit.md for findings/rejections and icon-audit.md for verified glyphs. Glyph replacement is a separate chrome change, not an animation dependency.

Final verification: `make test` passes, including native Scene visibility and noninteractive hiding, motion reversal, keyboard disclosure/reveal, pointer popup text entry during the fade, and keyboard navigation snapping opaque. Native captures were inspected. The full icon audit also corrected the component trash glyph.
