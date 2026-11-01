# Archive `src/graph` and graph-format assets, then reintroduce in Chibi

Owner: core-math session. Decided by the owner on 2026-10-29: "I'm fine accepting the loss of
functionality, and reintroducing it as we go."

## Now: full archive (option A)

- Move `src/graph` and the graph-format asset kinds (materials, fields, shapes, models,
  shader libraries, events) to `archive/<original path>`, excluded from build, format and tests.
  `archive/README.md` explains why.
- Cut their call sites in `src/authoring`, `src/assets`, `src/runtime/{models,fields}`,
  `shader_prepare`, `model_import`, `material_bake`, the previews and
  `src/automation/controller.c`. Drop those asset kinds from the project loader and scene inputs.
- Archive the fixtures and tests that exercise them (the ~30 fixture directories, plus the
  `test/render`, `test/authoring` and `test/graph` cases).
- Start from WIP branch `codex/archive-src-graph-20261029`, rebased on main. Get it building,
  merge immediately, fix forward. List what no longer works in the merge note.

## Then: nothing here

Owner, 2026-10-29: hold off on reintroducing these; a later session handles it. See `focus.md`.
