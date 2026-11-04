# Native archive invalidation and recovery

Code tested: `083e82408a337787cfd9f08c1fff1113a1884831`, tree `39424a0ad887d48fc7ccd389716963c375c90c08`. The original `test/native_aot/asset_compiler/Archive.chibi` and seven cases from `archive.sh` passed against the admitted current Linux compiler. This is an archive cache/recovery proof, not Windows execution, facade behavior, a full SDK test or PR readiness. PR #1331 remains held.

## What ran

A fresh isolated copy contained the exact 274 archive inputs plus the original helper and script. No generated cache was imported. The helper was compiled at the authored optimization level 0, independently admitted as an x86-64 ELF, then executed through a separately accepted seven-case phase. The genuine cold Graph path compiled and executed its normal asset-native builder. No synthetic receipt or prewarmed runner replaced that path.

- Cold: exit 0, one actual archive build, 8.330 seconds.
- Warm: exit 0, no build, unchanged outputs and runner, 0.032 seconds.
- Append the authored comment to the private Windows runtime template: exit 0, one build, 5.250 seconds.
- Edited warm: exit 0, no build, unchanged outputs and runner, 0.037 seconds.
- Remove that template: expected exit 1, no build, prior archive/JSON/record and runner preserved, 0.001 seconds.
- Restore the exact original template bytes: exit 0, one build, 4.888 seconds.
- Restored warm: exit 0, no build, unchanged outputs and runner, 0.032 seconds.

The three builds had distinct output stat fingerprints. A Windows-template comment changes the source receipt on Linux without necessarily changing Linux archive bytes; the proof checks that distinction rather than requiring an unrelated binary change. The runner remains reusable because its key does not include that template, while the genuine 275-input archive record does.

## Validation

The helper compilation took 2.929 seconds; its complete construction phase reached terminal-write start after 6.406 seconds. The seven-case phase reached terminal-write start after 22.452 seconds, within its original 300-second total limit including five seconds of cleanup. Both actual tool invocations exited 0, with normal owned retirement, authoritative ECHILD and no signals, errors or remaining children. These are correctness-run timings, not a performance benchmark. CPU affinity was two CPUs; authored default inner job selection was unchanged and could request nine workers.

Independent final readback verified all seven outcomes, the genuine ordered 275-input Graph record, final source restoration, exact permitted file growth, source/tool/runtime closure and all 7,571 preserved paths from the earlier product proofs. The final archive exports exactly `asset_compiler_compile_closure` and `asset_compiler_compile_closure_cached`. Controller evidence is retained; original inner compiler/builder cleanup still removes some ordinary-failure intermediates.

## Identities

- Current compiler: `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`.
- Archive helper: `25058aed015174cbf41c7a4c20df52ed17498ff04194168d274c05203cc2a7a1`.
- Final facade archive: `a80f06616c5f477cf35fecb9b7ac588b6862b0e0f37305142b898955a530d5ac`.
- Final archive JSON: `3391a40300da53c4a4c481cb4dad0b0b7d96c78bfd08c2aeb861d521bdf3cf28`.
- Independent final admission: `12b6492099329471c14b9b1decccf8cc193439796815619f44e6dfadb3421c8f`.
- Verified proof archive `chibi-archive-invalidation-validated-20261104.tar.gz`: `d1eb1fd16e7516c538cea41fcfdab0aaaea6a3d937fdaf979c0616fe92cf38c9`, 8,051,579 bytes and 613 regular members. It contains exact selected sources, controls, artifacts, receipts and independent reviews. External tools and original product trees remain separately pinned.

Still outstanding: C asset bridge behavior, SDK installation/relocation, ordinary VM/native differential, C engine integration, actual Windows runtime, separate pointer-view WIP and exact-head integration/merge gates. The terminal-unknown old bootstrap and excluded union leak investigation were not rerun.
