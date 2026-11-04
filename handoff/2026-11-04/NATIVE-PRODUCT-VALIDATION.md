# Native Linux product construction

Code tested: `083e82408a337787cfd9f08c1fff1113a1884831`, tree `39424a0ad887d48fc7ccd389716963c375c90c08` (4,072 authored files). This checkpoint records Linux construction, not installed SDK, bridge behavior, archive invalidation, ordinary VM/native differential, C engine or Windows runtime acceptance. PR #1331 remains held.

## Actual Graph path

The real Graph dependency chain selects `chibi`, `chibi-asset-compiler` and `chibi_asset_compiler_host`. The compiler target also stages native source/resources and the pinned FreeType archive. The first plan's assumption that selecting companions omitted `chibi` was rejected before execution. No dependency bypass or fabricated cache record was used.

The Linux third-party archive matched the authored release pin `1ef571b800ac45d9a0f7adce338ae6ed153f2f460883dbb2a595509a9c1648d7`. All 964 regular files and 500 directories were validated before safe extraction. This proof copied the FreeType archive as resource data; it did not execute downloaded code or enter the C build path.

## Preserved first attempt

The first actual attempt finished after 108.052 seconds with a failed controller result, clean ECHILD and no remaining children, signals or cleanup errors. Graph had rebuilt the compiler successfully and its bytes matched the admitted compiler. The post-build source fence then rejected three legitimate Graph cache files under `build/native/O2-7c45058c1234c2d6`. Companions had not started. This was an inventory omission, not a compiler failure or timeout.

Independent inspection then verified every authored file, tool/dependency fence, staged source/resource/metadata, the raw-equal compiler, its genuine 248-input Graph receipt and all 30 source/tool/runtime digest bindings in the runner cache. The complete failed attempt was preserved as a verified read-only snapshot before continuation. Its failed result was not changed into a pass.

## Separate companion continuation

A separately reviewed companion-only phase retained the original absolute source/product paths and actual compiler/cache records. It required the compiler bytes, full stat fingerprint and genuine receipt to remain unchanged. Authored files remained exhaustively checked, with only the exact three already-admitted runner files added to the source inventory. The normal Graph was permitted eight precise new paths and two rewritten command-metadata files. No full compiler rebuild or synthetic receipt was needed.

The real Graph command passed in **75.571 seconds**; the complete phase, including checks, took **84.792 seconds**. It retained its own 300-second total limit, with a 295-second execution cutoff and five-second cleanup reserve. This did not extend or reset the terminal first attempt. Two-CPU affinity constrained execution; inner helper compilation could still request nine workers, so this is not a two-worker claim or a performance benchmark.

Independent final admission verified:

- All 1,253 inventoried artifact hashes, exact permitted path growth and unchanged authored/tool/cache/preservation evidence.
- Genuine 248-input asset compiler and 275-input archive Graph records, plus unchanged compiler reuse.
- An x86-64 ELF asset compiler depending only on libc and the pinned loader.
- A regular archive with `compiler.o`, the two intended public closure entry symbols and matching archive/header/source receipt hashes.
- Empty C compile-command inventory, exact native link commands and no leftover temporary outputs.
- Actual tool exit 0, owner pass, authoritative ECHILD, respected deadline and no remaining children or cleanup errors.

## Artifact identities

- Reused compiler: `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266` (11,576,992 bytes).
- Asset compiler: `99101d9099b052a2d9a35817082631981c87828a108b3245c6c5c1f44f0adf77` (8,181,528 bytes).
- Facade archive: `a80f06616c5f477cf35fecb9b7ac588b6862b0e0f37305142b898955a530d5ac` (146,978 bytes).
- Archive JSON receipt: `3391a40300da53c4a4c481cb4dad0b0b7d96c78bfd08c2aeb861d521bdf3cf28`.
- Independent final admission: `93427cf7a1a4a8308af9d34f20089943dbd34d181866e812759672b12b76f059`.
- Preserved failed-attempt snapshot: `11fa22fb8690588f045ff115433eea466763f1919bff581d92d1f7f8b217a0e8`.
- Combined proof archive `chibi-native-products-validated-20261104.tar.gz`: `fb98425f24eb5e282b6bdcca0932f20715d9d50fff585722610b77863d2a53ee` (74,266,278 bytes, 51 members, including the full preserved snapshot). External admitted compiler/tools remain separately pinned.

Next: the focused Linux C asset bridge, then the separate archive invalidation and SDK installation/relocation gates. The bridge's optional legacy comparison is not enabled by its ordinary invocation, so it cannot substitute for a fresh VM/native differential. Actual Windows runtime and the separate pointer-view WIP also remain outstanding. The old terminal-unknown bootstrap and excluded union leak investigation were not rerun. No publication or merge readiness is claimed.
