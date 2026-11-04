# Portable dynamic-array cookie

The cold Linux compiler-host build exposed a real GCC warning-as-error: `DYNA_COOKIE` used the implementation-defined multi-character literal `'DYNA'`. The change replaces only that expression with unsigned shifts of four single-character constants, preserving the supported ASCII-host value `0x44594e41`. Header layout, allocation behavior and both production writers are unchanged. There is no added production work or performance claim.

## Focused regression

The new real C fixture pins the numeric value and checks growth across the initial sixteen-element capacity, copied contents, separate copy storage and free-to-NULL. Its checks survive `NDEBUG`. The existing graph-c selector now includes checked and release compile/run cases without removing or weakening its prior assertions, warnings or child deadlines. Full graph-c execution remains pending.

Independent source/control review preceded the isolated three-command GCC14 phase. The preserved old header failed with exactly the two intended multichar errors and no executable. The fixed checked and NDEBUG builds both passed with empty streams. Their compile times were 0.1145 and 0.1152 seconds; the original baseline rejection took 0.1196 seconds. The entire owned phase took 3.4613 seconds within its original fifty-second total limit.

Separate independent ELF inspection admitted only the two actual outputs. Both are x86-64 PIE executables with the expected Linux interpreter, libc only and no RPATH/RUNPATH. The checked build retains `__assert_fail`; release does not. Only then did the two real executions run, each returning zero with empty streams, in 0.00153 and 0.00138 seconds. The entire run phase took 3.1187 seconds within its original fifteen-second total limit. These are correctness-run timings, not benchmarks.

Both original phase limits included five seconds reserved for cleanup and were never reset or extended. Both owners reached authoritative ECHILD with no errors, signals or remaining children. Independent final review checked complete source/tool/search/Python identities, artifact byte/stat identities and all twenty-seven authoritative evidence files, including final evidence timestamps inside the original deadlines.

Final independent proof SHA-256: `8ccc11a3d9459f693d92244be7306e8e7af0a08e42fa2208de65c07113728520`. Exact source hashes, ELF hashes and phase results are in `array-cookie-results.json`.

Verified archive `chibi-array-cookie-validated-20261104.tar.gz`: `0559d6d36cc6b63afc37be337e1abef1384c8860697d92fdb0a5f430ee5dfb7c`, 17,438,584 bytes, 4,140 regular files and one preserved source symlink. Every member byte, mode and link target was reread. The inherited closure manifests are also preserved in the separate failed-host archive; installed external tools remain pinned inputs.

## Preserved failure and remaining work

The preceding compiler-host attempt remains failed and preserved in `chibi-compiler-host-failure-20261104.tar.gz` (SHA-256 `1f60f203915e644a8c00251f5b4308c2a37edf57aeedd7630473a48c52c7b1f7`). It stopped after 0.3515 seconds in Graph compilation, 2.3931 seconds overall, with clean ECHILD and no host executable. This focused proof does not turn that attempt into a successful host build.

That failed attempt also exposed a separate proof-format mismatch: the existing native Graph serializes POSIX stat words as signed low 32-bit values, while the original comparator expected full-width Python fields. A separately reviewed successor comparator models that exact serialization while retaining full-width independent identity and byte checks. This does not fix or prove native/interpreted filesystem-stamp compatibility.

The full graph-c runtime is still unarmed: its authored cancellation test intentionally kills a shell/sleep process group, which can create adopted descendants that the existing generic owner rejects. A precise ownership design must establish the real cleanup contract without hiding descendants or changing the test. Fresh compiler-host reconstruction, VM/native parity, C integration, actual Windows execution and exact-head integration remain separate gates. PR #1331 remains held. The old terminal-unknown bootstrap and excluded union-leak investigation were not rerun.
