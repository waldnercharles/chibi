# Handoff: profile-and-cut-the-comp (2026-11-02)

- Branch `claude/native-driver-options`, draft PR #1324, last pushed commit ea781f847. Worktree /Users/cwaldner/Personal/chibi/.worktrees/compiler-self-speed/chibi is clean on it.
- Done and merged: #1273 (linear LLVM emission), #1279 (scalar record ordinal), #1306 (split native modules, parallel opt/llc), #1317 (vendored archive rules in native_link). Numbers are in the side-session notes, compiler-self-compile-speed.md.
- In flight: #1324, gate 2 work in progress. It adds `--level`, `--lines`, `--static`, `--shared` and archive linking to native_driver Main, and doesn't compile yet: Main.chibi:162 `library :=` uses a keyword, so rename the variable. It is based on #1315 (strings-ints) merged with main. No CI runs of mine are pending.
- Next: fix the rename, build stage one, then run /tmp/compiler-self-speed/gate2/samples.sh DRIVER (the foreign samples plus the options) and merge. After that: `--root` discovery, the output cache and receipts BuildTests.chibi checks (dynamic by default on Linux, `linkage` lines), `--target` cross links, then switch `chibi build --native`/`run --native` to the native driver and delete `Native`.
- Local only: /tmp/compiler-self-speed (about 300 MB of scripts, profiling hosts and compiler caches). Only gate2/samples.sh and emit/capped.sh matter, and both are easy to rewrite.
