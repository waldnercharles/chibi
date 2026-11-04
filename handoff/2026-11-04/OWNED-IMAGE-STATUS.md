# Owned-record image validation status

The exact takeover source `02c86287430def964445ec90185961e20319536d` now passes the focused Windows cross-link/PE gate and eight Linux owned-record image preservation cases. Actual Windows execution remains separate.

## Accepted fresh proof

A separately reviewed revision corrected only the Windows builder's import policy and reused its exact already-built bytes after new source/tool/receipt/PE admission. The failed predecessor was not resumed or relabeled.

- All **30 new commands**, **14 PE inspections** and **eight Linux cases** passed. The inventory contains 137 files, including the receipt.
- The two real Windows helpers and twelve DLLs are AMD64 PE32+. Their exact imports/exports match their distinct source-derived roles. The bcrypt exception applies only to the exact builder executable and exactly `BCryptGenRandom`; host and DLL rules were not widened.
- Borrowed and owned pointees pass at O0/O2. Four healthy cases return zero with empty output. Four one-import-omission controls return exactly 33 with `owned record Text owner missing before unload`.
- The negative controls detect missing ownership before inspecting Text bytes, release all roots and collect both images to zero shared allocations/cells while both remain mapped. They do not manufacture a dangling pointer or unload an image early.
- The new phase ran from 08:58:15 to 09:06:48.374 UTC, **513.374 seconds**, inside its original 600-second bound. It retained the five-second internal cleanup reserve. Controller, watchdog and actual owner all returned zero; authoritative empty-child termination and unchanged guards passed.
- Independent final review rehashed the source/tool/runtime/rebuild/reuse closure, all outputs and preserved predecessor files, reconstructed every command and generated transform, and separately parsed every PE binary. Its admission record is SHA-256 `c989ba480455c0ab19856488ca8e28cd0e42caedf5c1e8f57138bcef25c51a12`.

The local 243-file evidence archive is SHA-256 `ce5debe7b53a1339a58d22ce18de923bd0c8a7ab37e813a81b5d48bae19ffc56`. It preserves the failed predecessor and accepted fresh proof; source/compiler/tool packages remain separately pinned. `owned-image-results.json` records the accepted scope and receipt identities. These elapsed times are correctness diagnostics, not a quiet benchmark.

## Preserved first attempt

- The helper compiled successfully with empty output in 167.189 seconds, using one LLVM worker. Its SHA-256 is `e338ca99ec95532d43e97a2bbbca0dd8c2fcc6abde919f68dc9c3f503e847d74` (11,792,896 bytes).
- `llvm-readobj` returned zero and identified AMD64 PE32+ with `kernel32.dll`, `ucrtbase.dll`, and `bcrypt.dll` imports.
- The proof rejected `bcrypt.dll` before recording any successful PE admission. Its only bcrypt symbol is `BCryptGenRandom`.
- Independent source review traced that symbol to `packages/core/sys/windows/Windows.chibi`: `TemporaryDirectory` calls the explicitly declared `system:bcrypt` function. The builder includes that core/compiler closure. The proof's builder import policy was incomplete; this is not evidence of an unexpected compiler-generated dependency.
- The original phase started at 08:32:17 UTC on November 4 and retained its 08:42:17 deadline. It stopped after 183.929 seconds with actual owner exit 1, raw child exit 1, no adopted children, no signals or owner errors, and authoritative empty-child termination. The deadline was respected.
- No Linux image runtime case, second helper build or Windows image execution ran. The failed attempt and its original controller remain unchanged.

The accepted fresh proof above resolves this narrow validation-policy issue. This first attempt retains its failed status and original deadline.

## Remaining gates

Actual Windows loading/unloading, ordinary/native differential, product/integration checks and PR #1331's explicit hold remain. Neither the old terminal-unknown bootstrap nor the excluded union-native leak investigation was retried or declared cleared. The first helper's elapsed time is a bounded correctness diagnostic, not a quiet performance measurement.
