# builtin-math handoff (2026-11-02)
- Native tasks: branch codex/native-tasks-prototype, last pushed a3f05c9bf, draft PR opened (see gh pr list); design is PR #1284 (docs, open, owner's 3 questions unanswered: task ownership, swap vs live tasks, minimum LLVM).
- Done and merged: #1144 (C callbacks, parallel native-build), #1195 (rawptr authored type, `foreign { }` with no library), #1201 (package-wide scoped pointer table), #1184 (friction note).
- In flight: nothing running. Prototype is untested: needs ScalarDiscarded fix for discarded spawns (not applied), then recover the compiler and run a Main that spawns two Workers, yields twice and checks the Log order (program is in .chibi/math-main/fnptr3/t1, local only).
- Next: apply the discarded-spawn fix, run the t1 program at O0/O2 on macOS (IR via fnptr3 harness, local only), then LLVM 18 coro.end i1 rewrite in the driver, then a native-build case.
- Local-only: .chibi/math-main/fnptr*, fnptr3 scratch harness (iterate.sh/run.sh) and /tmp/tp patch scripts; not needed, the tree has the changes. Pinned hosts there need refreshing per tools/compiler/runtime.json and tools/bootstrap/hosts.tsv.
