# Archive

Features removed from the build but kept for reference and later revival. Paths mirror the original tree: `archive/src/authoring/x.c` was `src/authoring/x.c`.

Nothing here is built, formatted, tested or listed in an inventory. Bring a file back with `git mv` to its original path and re-add it to the build lists.

## Studio editor services (authoring)

Editing, rename, completion/hover/highlighting and prototype-source editing for Studio. The compiler, formatter and reference index do not need them.

- `src/authoring/source_edit*`: refactoring and source edits (the text helpers `source_minimize` needs moved to `source_refactor_text`).
- `src/authoring/source_intelligence*`: highlighting, completion and hover.
- `src/authoring/source_rename*`: rename plans.
- `src/authoring/source_prototype_edit*`: prototype source edits.
- `test/authoring/source_edit.c`: their test.
