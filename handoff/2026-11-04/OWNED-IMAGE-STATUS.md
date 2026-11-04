# Owned-record image validation status

The exact takeover source `02c86287430def964445ec90185961e20319536d` has now cross-linked the real Windows `OwnedRecordImages.exe` helper using the independently rebuilt current compiler. This is partial evidence, not Windows execution or completed image acceptance.

## Preserved first attempt

- The helper compiled successfully with empty output in 167.189 seconds, using one LLVM worker. Its SHA-256 is `e338ca99ec95532d43e97a2bbbca0dd8c2fcc6abde919f68dc9c3f503e847d74` (11,792,896 bytes).
- `llvm-readobj` returned zero and identified AMD64 PE32+ with `kernel32.dll`, `ucrtbase.dll`, and `bcrypt.dll` imports.
- The proof rejected `bcrypt.dll` before recording any successful PE admission. Its only bcrypt symbol is `BCryptGenRandom`.
- Independent source review traced that symbol to `packages/core/sys/windows/Windows.chibi`: `TemporaryDirectory` calls the explicitly declared `system:bcrypt` function. The builder includes that core/compiler closure. The proof's builder import policy was incomplete; this is not evidence of an unexpected compiler-generated dependency.
- The original phase started at 08:32:17 UTC on November 4 and retained its 08:42:17 deadline. It stopped after 183.929 seconds with actual owner exit 1, raw child exit 1, no adopted children, no signals or owner errors, and authoritative empty-child termination. The deadline was respected.
- No Linux image runtime case, second helper build or Windows image execution ran. The failed attempt and its original controller remain unchanged.

The next step is a narrowly reviewed builder-only PE correction, followed by fresh source/tool/receipt admission of the existing executable before any reuse. It must require exactly `BCryptGenRandom` from bcrypt; host and generated-DLL policies remain separate. A new bounded proof will describe its actual command inventory and reused-artifact provenance rather than relabel this failed attempt or extend its deadline.

## Remaining gates

The eight Linux owned/borrowed image cases, complete PE inventory, actual Windows loading/unloading, ordinary/native differential, product/integration checks and PR #1331's explicit hold remain. Neither the old terminal-unknown bootstrap nor the excluded union-native leak investigation was retried or declared cleared. The first helper's elapsed time is a bounded correctness diagnostic, not a quiet performance measurement.
