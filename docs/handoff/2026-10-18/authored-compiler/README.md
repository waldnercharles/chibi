# Authored compiler source-only handoff — 2026-10-18

This draft checkpoint preserves the existing work for handoff at the user's
request. It is **not build-validated, runtime-validated, integrated, or ready to
merge**. The ordinary pre-publication build/test sequence was deferred solely
for this source-only handoff; no merge, approval, protection or dependency hold
is waived. Keep the feature branch.

## Exact checkpoint

- Repository: private `waldnercharles/chibi`.
- Branch: `codex/m5-authored-compiler-handoff-20261018`.
- Parent: `c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`.
- Draft PR base: `codex/toolchain-cleanup-20261017`.
- Twelve complete new files are present at their intended repository paths.
  `manifest.json` records their frozen SHA-256 and Git blob identities.
- The full original source/registration/PLAN proposal is preserved as
  `authored-compiler-driver.patch`, SHA-256
  `1b6db1fa96f49358d4bf05c4e8ed8d3533900142a1782f7c7d460c0dc9e19924`.

The new compiler, native bridge and test files are intentionally **unregistered**
pending dependency integration. No existing source, CMake, dispatcher, policy or
PLAN file is changed. `driver-tests/registration-only.patch` and
`plan-checkpoint.patch` are unapplied historical proposals. Do not apply their
contexts blindly: integrate against the actual dependency combination and
preserve all current registrations. The new `.cmake` file is not included by
the existing build. No source dependency is copied into an existing path.

## Required dependency heads and known results

- [PR #445](https://github.com/waldnercharles/chibi/pull/445):
  `e165059d1c17ec7605d909ae3bbf727d9e267a80`. Ordinary build passed;
  runtime-only boundary audit failed; runtime test skipped.
- [PR #446](https://github.com/waldnercharles/chibi/pull/446):
  `70ff5bf54a1125a110be6a26a7e226a4b5d542ba`. Runtime validation unrun.
- [PR #447](https://github.com/waldnercharles/chibi/pull/447):
  `5c27f2b00b4cd4c7385b06d118217c6b8abe8ff8`. Scalar source syntax fixed
  and its parser-only check passed; full scalar runtime validation unrun.

These results belong to their separately recorded dependency work, not this
checkpoint. Dependency heads were re-read before publication. Historical notes
and `manifest.json` retain the older source-reference commits used to prepare
the packet. Those provenance pins do not supersede the current heads above.
Neither PR #407 nor PR #420 is imported or cleared.

## Review and execution status

The complete 18-file compiler-closure parser precheck was independently admitted
for one separate build and one complete supervised invocation. It was **NOT
RUN**, because work stopped for this handoff. Original frozen packet SHA-256:
`a2d920d9d4f3745f2071499aa038216d1ab9f6b45694fee04f94948399957149`.
The review record is `review/independent-review-checkpoint.md`; its original
pre-publication source SHA-256 was
`2d6e1fe96d8d9ff960ada608782bdb3cdfaf4aea5ba63837d90a66d6c771edfa`.
Only coordination wording was removed from the published technical record.

Substantive compiler correctness, native producer/fixture and full test-workload
review remain unfinished. No combined production tree has been constructed or
validated. This publication performed source identity, inventory and
credential-pattern inspection only; it started no configure/build, parser
invocation, product test, workflow dispatch, VM/JIT, or Studio session.
Historical formatting and patch-application checks are documented separately;
they are not product test passes.

The proposed test is one C/pico case: 78 authored Compile calls, 106 total
parses, 28 payload callbacks, seven direct bridge rejections and 56 target
invocations. These counts remain proposed and incompletely audited. The full
compiler closure is 18 files, 16 packages and 179,267 UTF-8 bytes. Same-process
fresh host API consumers do not establish runtime-only executable isolation.

## Recovering the work

- `HANDOFF.md`: compiler subset, implementation and integration details.
- `native-bridge/HANDOFF.md`: ABI, ownership and transitional native dependencies.
- `driver-tests/HANDOFF.md` and `source-manifest.json`: finite test proposal.
- `syntax-precheck/`: C diagnostic source, original commands, corpus/source
  identities, frozen checksums and toolchain provenance. See `RECOVERY.md`.
- `checkpoint-inventory.json`: every other published file's bytes, SHA-256 and
  Git blob identity; generated solely for handoff verification.

Large copied parser/runtime references, duplicated dependency sources, generated
decimal source arrays, old superseded candidates, binaries and redundant patch
copies are omitted. Immutable repository revisions plus source manifests and
the exact current files recover the substantive work. Historical technical
notes can mention files from the original preparation layout that are not
duplicated here. `RECOVERY.md` explains diagnostic reconstruction and the need
to validate/admit a reconstructed packet before executing it.

Next steps are to resolve dependency validation/holds, finish independent
contract and workload review, construct and freeze an exact integration tree,
then separately admit and run the applicable builds and bounded checks under
the unchanged shared 60-second production deadline. Never split or reconstruct
a timed-out workload through subsets.

This is a bounded one-package scalar compiler prototype. Full language
self-hosting, stage-one/stage-two bootstrap, an installed standalone producer,
and a final build without CMake/Clang/GCC are absent. Native finalization,
verification, optimization, module publication and inner serialization remain
explicit transitional dependencies. No recovery path is removed.
