# Handoff: CI keeper (cleanup-781, local_76d6748b-121f-4a3e-b441-926d5b3a50f0), 2026-11-02

- Branch/PR: claude/pin-native-seed-60901f2fa, PR #1326 (unmerged, paused), last pushed commit 130ff46cc. Everything else is merged (last: #1312 910eff2c1, #1311 329e0f077, #1310 70dccb2bb).
- Done: native seed (goal item 7). native-seed-5621c10e8 is pinned for linux-x64 and macos-arm64 in tools/bootstrap/native-seed.tsv (#1295, #1303, #1308). Recovery starts from it; #1312 adds stage four when the seed is older than the sources. Green: run 41347808206 (linux stages 3 and 4 = 73748fed…, macOS = ad52ecde…). Main CI green on 910eff2c1 (run 41347787818).
- In flight: none. Native-seed publish run 41357775253 (60901f2fa) is green: linux-x64 a012df37…, macos-arm64 6f640d56…, stage three = stage four, and stage four ran in about a minute. Its pins are in PR #1326; merge it when unpaused. Windows: strings-ints' claude/windows-self-build; add and pin a windows-x64 leg in native-seed.yml after it is green.
- Next step: merge #1326, then tell the coordinator the main commit so it can collect items 1, 2, 7 and 9. Seed and pin rules are in sessions.md.
- Local only: /tmp/ci-keeper (scratch logs, xabi/xnat cross-seed inputs, the seedwt worktree, which is clean and detached at 329e0f077, and watch.sh). None of it matters; it can be deleted.
