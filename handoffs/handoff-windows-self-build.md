# Handoff: windows-self-build (goal item 8)

- Branch `claude/windows-self-build`, draft PR #1315, last pushed commit b3a4180aa (worktree .worktrees/authored-strings-regressions/chibi, clean).
- Done: `native-self-host --windows DIR` cross-links stage two for windows-x64 (lld-link, import libs from `; chibi-import` markers, `/stack:67108864`); `driver_paths.Visible` maps `C:/x` to `/C/x` for the compiler's POSIX-only roots; `native-self-host.yml` has a windows-2022 job (stage 2 artifact -> stage 3 -> stage 4, requires 3 == 4). Locally, cross-link works (33 s, imports only kernel32.dll).
- In flight: CI run 41358702975 (dispatch at b3a4180aa); earlier runs found, in order: missing imports, `C:/` roots, 1 MiB stack overflow (STATUS_STACK_OVERFLOW). Whether stage 3 == stage 4 on windows-2022 is not yet known.
- Next: read that run's Windows job log; fix forward on the branch; when 3 == 4, mark #1315 ready, merge, tell the CI keeper (cleanup-781) to add windows-x64 to native-seed.yml, then delete the frozen-runtime Windows leg in `.github/workflows/compiler-recovery.yml`.
- Only on local disk: /tmp/w22-llvm (llc, lld-link dir for local cross-links), /tmp/winstage*.sh, /tmp/wsh-* scratch; none matter.
