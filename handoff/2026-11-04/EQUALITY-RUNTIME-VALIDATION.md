# Focused equality runtime validation

The cleanup in b5c03fb445fc86430707d4c8dcbe2c06e45a182b now has separate, independently admitted runtime assurance in addition to its strict real-unit compile regression.

Ten strict GCC14 GNU23/O3 compilations (nine complete runtime units plus a fixed driver) and the actual link passed with empty diagnostics in a 6.975-second phase. No replacement equality algorithm or mocked equality callee was used. The driver uses the private host's immutable empty catalog convention and borrows stack/static fixture values; the equality table retains its real allocation/free path.

The first metadata phase was correctly rejected because its bounded import list omitted libm's trunc. That actual failure remains preserved, with no successful final post-fence/result credited. Independent source/export review traced trunc to existing field-range validation in the real linked closure. A fresh inspection/runtime-only continuation added exactly that supported symbol, bound the already built immutable artifact and performed no recompilation. It did not reset or reclassify the consumed failure.

The fresh metadata gate passed in 3.234 seconds: expected x86-64 executable and loader, only pinned libm/libc dependencies, 24 ordinary imports, no search/audit/filter hooks, and exact source/artifact/build records. Runtime then passed in 0.001970 seconds within a 3.305-second phase, with the exact 94-byte success line and empty stderr. Cases cover scalar/type equality, signed zero and NaN, flat/packed arrays, repeated-call cleanup, pair-table growth, shared aliases and cyclic nested arrays. These are correctness observations, not benchmarks.

Independent final review reconciled all original bounds, launch/wait identities, ECHILD, full source/tool/input facts, prior build/inspection inventories, exact output and artifact identities, and every in-packet evidence write. External terminal captures are explicitly later evidence, not backdated phase writes. The unchanged owner reported no signals, errors or surviving children. Both proof archives were fully reread against original member bytes/modes/identities.

`equality-runtime-results.json` records the exact digests, archives and limits. The build and original failed inspection archive contains the binary/source evidence; the successful continuation archive references those immutable prerequisites and the pinned installed toolchain.

This is narrow scalar/array behavior assurance, not proof of a pre-fix runtime defect, GCC's internal diagnostic cause, sanitizer/leak coverage, a complete host build, union behavior or integration readiness. No broad types selector or excluded union leak investigation ran. Full host and graph-c gates, actual Windows and the explicit PR #1331 hold remain outstanding.
