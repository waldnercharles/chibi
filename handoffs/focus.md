# Current focus (owner, 2026-10-29; superseded in part by goal.md, 2026-10-30)

Read `goal.md` first: the owner's `/goal` now puts all first-party C in scope, the engine included.

"The primary goal right now is to get our compiler to just compile itself, and eliminate
as much C and CMake as possible. We can break things in the process."

1. **The compiler compiles itself with no C in the loop.** Today the self-hosted compiler
   (Chibi, `tools/compiler`) compiles itself, but runs on the C VM and runtime, parses with
   the tree-sitter C parser, and still has the C front end (stage zero) beside it. Targets:
   - parse with the Chibi parser (#844 merged). The owner said hold the switch until it is
     fast (total compile within ~2x today). Measured: 1.24x, so the bar is met; the switch (#867)
     merged 2026-10-30 as 4d59a21be. Deleting `parser.c`/`syntax.c` and the vendor needs, in
     order: #883 (old-formats, archive the game-compile path; then strings-ints archives the UI
     source parser and css/html vendors), #884/#885 plus the `chibi test` cut-over (coordinator),
     then archive `src/language`, the `source_*` files and tree-sitter with the three pins;
   - retire stage zero (`src/language`) and the C prepare/compile paths in `src/authoring`;
   - compile the compiler natively (native backend, `docs/native-llvm-ledger.md` M4-M7,
     W21/W22), so `chibi-compiler-host` stops depending on the C VM. Split: sdk-cmake W7
     arrays then W14, builtin-math W9 managed fields, core-math W13 imports then W12
     constants and census reruns (W8 maps skipped: the compiler uses no maps). One LLVM-lane writer at a time.
   - Owner: prioritize language features that make Chibi code fast (for the parser and
     compiler first): strings-ints ranks them from parser profiles; generics/records build them.
2. **Delete or archive as much C and CMake as possible**, plus the Makefile and Python.
   One `chibi build` command, Zig-style.
3. **Not now:** reintroducing archived features (graph assets, materials, shaders, models,
   Studio editor services). The engine C itself is now in scope (`goal.md` item 4): archive it
   or port it. Breaking games, Studio or platforms is fine.

Rules: merge as soon as a change builds; fix main forward. Archive whole features under
`archive/<original path>`; delete dead code. Follow Zig/Odin prior art when unsure.
