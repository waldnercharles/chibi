# Cold Linux SDK installation and relocation

Code tested: `083e82408a337787cfd9f08c1fff1113a1884831`, tree `39424a0ad887d48fc7ccd389716963c375c90c08`. The complete unchanged registered `graph-sdk-install` selector passed with the admitted current compiler and native products. Independent final review accepted its exact source, receipts, output artifacts and process cleanup. This closes the Linux SDK gate, not Windows execution, C engine integration or PR readiness. PR #1331 remains held.

## What ran

A fresh isolated copy contained 4,072 authored files, 964 verified dependency files and independent copies of the admitted compiler, asset service and facade archive. No old `build/native` or build-runner cache was imported, no receipt was rebased, and no helper was prewarmed. The original selector compiled and ran through the copied SDK compiler, with the original graph collection and pinned LLVM tools.

All 96 authored assertion call sites remained unchanged. The case-sensitive Linux path includes 30 CompilerTools invocations and two Install invocations. Its eight successful cold compilations cover the selector, CompilerTools, Install, staged Verify, relocated consumer, one-test package, project Build and installed Graph/Libraries. It also retains the intentional missing-LLVM and undefined-name compilation failures. Conditional assertions for a case-insensitive filesystem are not Linux coverage.

The suite exercised corrupt-product/source rejection, reserved stages and symlinks, installation and relocation, missing-tool rejection, relocated run/error/test/project-build commands, native archive validation, genuine repeated Graph cache use and removal of obsolete overlay files. Deliberate mutations and deletion stayed inside the private fixture. The original product proofs were preserved, including file identities and timestamps. Bare invocations clear PATH and CHIBI_CORE while retaining the explicitly pinned LLVM library path; they do not install anything on the host.

## Result and evidence

- Actual tool session 63247, terminal chunk `410431`, exited 0.
- Original selector: 134.945 seconds, exit 0, empty combined log.
- Recorded controller interval: 139.471 seconds, within the original 270-second bound, including the five-second cleanup reserve.
- Owner: normal root exit, authoritative ECHILD, no signals, errors or remaining children.
- Final source growth: exactly four genuine native cache directories, each containing program, receipt and LLVM source. Original authored/dependency files and copied products stayed unchanged.
- The authored temporary fixture was removed and private HOME/cache/temp directories were empty.

Independent post-run review rechecked complete source/product and original-proof inventories, tool/runtime/import identities, loader rules, the real cache receipts and actual program hashes. All four retained programs are Linux x86-64 ELF executables using the expected loader and libc, without RPATH/RUNPATH. Deleted relocated receipt paths map to exact preserved copied source; they are not fabricated cache hits.

This was one correctness run, not a benchmark. Affinity was CPUs 0 and 1, not a claim of two inner LLVM workers. Actual inner-worker count and peak disk use were not measured. Successful ephemeral Verify, consumer, test and Build artifacts were deleted by the unchanged authored cleanup; their execution is supported by the exact source-bound suite and its successful assertions, not retained binaries. Retained LLVM hashes identify files, not an independent source-to-IR equivalence proof.

## Identities

- Compiler: `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`.
- Independent final admission: `428c642ea42df43c8710df344c1629d8082531fc61d8e32446dd8156a7f5478e`.
- Verified proof archive `chibi-sdk-install-validated-20261104.tar.gz`: `b35b25c8bc538578b62fba4e554a6a86eb8021e9c8c98140d8512d28dc3689df`, 53,592,742 bytes, 5,378 regular members and one preserved source symlink. Every member was reread for exact bytes, mode and link target. It contains copied source/products/dependencies, controls, retained caches, receipts and independent reviews. External tools and original product proof trees remain separately pinned.

The archive invalidation and asset-bridge gates described by earlier checkpoints are also complete. Remaining work includes ordinary VM/native differential, C engine integration, actual Windows runtime, the separate pointer-view WIP and exact-head integration/merge gates. The terminal-unknown old bootstrap and excluded union leak investigation were not rerun.
