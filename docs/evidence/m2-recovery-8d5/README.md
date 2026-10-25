# Frozen8d5 production manifest and Linux source-view recipe

This evidence belongs to source commit **8d5ff52831e12f624a10c178ef9d9aef072881ee**, tree **60e9e42e76956db1b065d3ec331b9b5f7bf8fce9**. It is not acceptance of later stage-zero source composition. No compiler binary is included, and no checked seed or runtime pin is changed.

`bootstrap.production.json` is the exact, unmodified output of the production `chibi-compiler-host --seed` writer on Mac: **24,317bytes**, SHA256 **c2c6762a8a896a47f1a02b2f1c8ec730ef4564d7c548d2be0bb73d97f5fa03a6**. Its honest recorded revision is `8d5ff52831e12f624a10c178ef9d9aef072881ee-dirty`: a disposable source snapshot had only the two bootstrap inputs overlaid. All20 compiler/core source records match frozen8d5 Git blobs. Do not rewrite this manifest or its revision.

The corresponding prepared21 compiler closure is **13,663,537bytes**, SHA256 **9bb4f52e14c3ec7570b0054bc2526d1c9bd073b22cda17cb993ffde107f53ce1**. Mac direct historical-input recovery, the real stage-zero bootstrap fixture, production writer and current-source recovery yielded identical bytes. Root independently reports the same bytes from Linux direct recovery. Verify the Linux file hash/size before use; no Mac executable/binary transfer is needed.

## Linux source view (not the public checkout)

1. Export or create an isolated checkout of exact8d5. Preserve its original checked prepared20 pair separately; label the disposable checkout as8d5 plus the explicitly identified bootstrap inputs.
2. Copy the verified Linux-produced9bb4…53ce1 closure to disposable `tools/compiler/bootstrap/compiler.chibip`. Copy this exact `bootstrap.production.json` to disposable `tools/compiler/bootstrap/bootstrap.json`. These are the ONLY two tracked overlays. Preserve public branch seed/pin unchanged. Do not relabel historical inputs.
3. Configure/build using the normal standalone target and existing Linux LLVM/compiler/dependencies (up to2jobs):

```sh
cmake -S test/language -B build-recovery -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=<existing-linux-clang> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCHIBI_THIRD_PARTY_CACHE=<existing-pinned-cache>
cmake --build build-recovery --target test_language chibi-compiler-host --parallel 2
```

Mandatory asset generation stays enabled. No asset-disable flag, SDK bypass, source patch, quota change or extra overlay was required. Original target admission precedes source reuse/recovery. Compare/admit the manifest's exact target on the actual Linux host; a mismatched target must not be silently rewritten. Mac final generation reported bootstrap reuse status0.

4. Use normal supervision with BOTH `CHIBI_LANGUAGE_BUILD_DIR=build-recovery` and `CHIBI_RELEASE_LANGUAGE_BUILD_DIR=build-recovery`. Run the15 focused selectors enumerated with exact case/assertion counts in `commands-and-results.json`, plus Linux-owned native gates. The native scalar/LLVM/lazy-arm cases remain distinct from Mac bytecode/SLJIT execution. Run default asset host and stock source snapshot verification as recorded there.

`platform-source.json` and `target.json` capture actual Mac platform/tool versions, all20 source hashes and all6 target parts without losing integer precision. The JSON target file is a supplemental receipt, not a replacement bootstrap manifest. `commands-and-results.json` preserves actual commands (including Mac paths), per-selector counts and raw-log references; adapt only machine/output/compiler paths for Linux and record the actual Linux commands.

## Provenance of each pair

Original historical inputs came unchanged fromab8c4a58: closure13,390,570bytes/SHA25658d17229e51162cc713a09ff8330911b6d3f210ff9b12e91619ed9da7af77c06; manifest24,311bytes/SHA256389d01225837b69fc874ba82be0dd4f6a831ed0ff3329147b1d99e5e54f7523a, recorded revision2100e9adec9403825754df44613c561b011fbd5f. This lane did not generate or modify those files.

Direct historical-input recovery generated only a current closure. The existing real-driver bootstrap fixture generated seed closure plus compile.json. The production `--seed` writer alone generated the new current bootstrap.json supplied here, after verifying supplied closure/source/fixedpoint. Subsequent supported recovery emitted the same closure. The final normal driver/asset test snapshot differed from clean8d5 only by this newly generated current pair. No overlay beyond two bootstrap files was used.

## Executed scope

Mac Release15selectors:103cases/95,518assertions; bootstrap1/18,704; final asset-host3/21; final integer1/351; snapshot and recovery/fixedpoint passed. All compiler and emitted-backend details are in the accompanying command receipt and local detailed handoff.

Mac ASan+UBSan RelWithDebInfo: all505 C compile entries and executable links instrumented, cached matching vendor archive, no suppression environment;16selectors passed106cases/95,539assertions with no sanitizer reports. Seven isolated production Chibi mutations were independently verified caught in12selector runs. Pure reversed evaluation yielded123 instead of321 while static bindings stayed correct; that failing emitted bytecode call short-circuits its emitted SLJIT check. Receiver-watch omission was caught by metadata checks. These are frozen8d5 results, not later-composition results.

Linux Release direct recovery and bootstrap-recovery2cases/15assertions passed per root. Linux sanitizer build succeeded but LeakSanitizer failed at shutdown under cloud ptrace even for target-json; no Linux sanitizer recovery or equivalent leak coverage is claimed. Root owns Linux native validation with the approved installed lld.
