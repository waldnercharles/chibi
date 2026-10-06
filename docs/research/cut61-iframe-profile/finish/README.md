# CUT61 iframe optimization

Studio creates empty hosted documents directly, avoiding RML parsing and the
unrelated model updates performed by `Context::LoadDocument`. Fitted iframe
content refreshes geometry before height measurement without repeating
`OnUpdate` in the stable-box refresh. Existing full updates and four settling
passes remain. Stable attachment identities are resolved again after callbacks.

The current branch integrates main `0f629f8bcaefe9689ed0060e533835082897baa4`.
Its 60-second policy keeps the long lifecycle and broad UI/native/render/backend
execution quarantined. Only the exact CPU tuples `--iframe-updates` and
`--kit-documents` gain scoped admission, based on independently reviewed complete
measurements at `7b0389eabf87953b1ca533df5dc27fa9dc093983`. Fresh supervised
validation and the final measurement record are added after the code is frozen.

The admission evidence in `historical/clock-iframe.json` records 12 checks and
all 1000 optional profiling frames in 2.410 seconds.
`historical/context-clock/results.json` records all 10 original checks and 49
frames, alternating profiling enabled/disabled in six fresh processes, each
0.469–0.551 seconds. Both use Debug, ASan+UBSan and assertions on macOS arm64;
these fixtures perform CPU layout/hit testing and no GPU render work. The binary
SHA-256 is `869d923e57055c28eac11742b501897a214e1ccf0569fc3b8b5d6f7aa0e48109`;
`historical/environment-clock.json` pins source/tree, dependency patches,
compiler and build settings.

Historical lifecycle, browser and comparison artifacts retain their own source
identities. They do not verify the current integrated head. The older mixed-clock
second candidate capture passed its original lifecycle assertion but failed
profile reconciliation; its raw rows and failure are preserved.

Python test launchers have been removed. Workloads and selector checks are C;
research analysis reads saved logs only. No full gate or landing authority is
supplied by these focused results.
