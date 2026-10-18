# Snapshot-to-reader handoff checkpoint

Status on 2026-10-18: source-only reader implementation, with an unarmed
validation proposal saved for handoff. The validation helper and control have
not been independently admitted. No reader configuration, build, product-link
audit or runtime selector has run. This checkpoint is not landing-ready and
does not establish compiler or M5 self-hosting completion.

## Exact source and dependencies

- Private [PR446](https://github.com/waldnercharles/chibi/pull/446), branch
  `codex/m5-snapshot-reader-state-20261018`, remains draft.
- Original source head: `70ff5bf54a1125a110be6a26a7e226a4b5d542ba`;
  tree: `1ca14bfda693998e9769b552b84b91bbfc4cc6cb`.
- Original stack parent: `1338151c084c1a633f881153084bc83773df27fa`;
  tree: `6a31c1576aaab2579ebca70a0c0b076b5bd5e1e6`.
- Updated [PR445](https://github.com/waldnercharles/chibi/pull/445) transport
  dependency: `e165059d1c17ec7605d909ae3bbf727d9e267a80`, tree
  `8e5ea89dfe428ca7804b68af0c85ba35e5e74b5e`. It integrates landed semantic
  source through `c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`.
- The reader still uses its original stack. The updated transport and semantic
  sources must be integrated and reviewed later. They are not imported by this
  additive checkpoint.
- Historical proposal target: `codex/toolchain-cleanup-20261017` at
  `ace559c9c294a450c95a3a730dcd6d9f1b5ce13c`. This is a historical reference,
  not the current integration pin.

The source review applies only to the original eight-file reader patch. Its
patch SHA-256 is
`14e3ed6b9a2ee89430517440e2dd2cdf6d51536dd2b9148e3167a466e2170823`;
its recorded independent source review SHA-256 is
`638d12c0bb62ccbbf4dff308992f33e805e857aab6e81805ed6d7995ca3d561e`.
Neither is approval of the added validation proposal or a future integrated
candidate. All pre-existing repository blobs and modes are preserved.

## Saved proposal

- `.github/scripts/reader-state-validation.sh`: exact existing proposal,
  SHA-256 `30fcd4a85acd078f50ec8f46cf25d5c20825213efa530e07c8d6c07a8d68e555`.
- `.github/scripts/transport-boundary-audit.py`: unchanged reviewed dependency,
  Git blob `0482fd9afa2ad1e3f0edf0b332b826a9bf067574`, SHA-256
  `612f31e95ee749a308c8d37531cda67972d73cae6a1d26161e66761d4209cadd`.
- `proposal/toolchain-m5-unit-analysis.yml`: exact unarmed control, SHA-256
  `ea9cf7f94cc15e322fae00d11dbbec1fd869ee416c89ba7ad12b788f2a0e7098`.
  It is documentation, outside `.github/workflows`, and retains
  `READER_STATE_REVIEWED: false` and unresolved candidate SHA/tree placeholders.
- `admission.json`, `resource-manifest.json`, and `proposal-manifest.json`
  record state, original source accounting, preserved bytes and dependency
  identities. `recovered-files.json`, `reference-identities.json`, and
  `transport-recovered-files.json` identify immutable review inputs without
  committing redundant source copies.
- `check-controls.py` and `check-workflow.py` preserve the original synthetic
  checker code, unchanged. They are archived proposal checks, not an added
  production Python test suite or evidence of target execution.

The control still names the proposed future private validation ref
`refs/heads/codex/m5-snapshot-reader-validation-20261018`, not this checkpoint
branch. Its historical Studio PR81 reference is
`02894277346e9c3d8dafdf56b48497b93c9a0052`, tree
`ee674c048a629cc1d8fc72f19b7fa4809f44d40c`; base
`bee5b54e159cce44ee52ca9439be587d9a95f5d2`. These are historical inputs, not
authorization or current pins for a run. No Studio branch, workflow or run is
created or changed by this checkpoint.

## Evidence and remaining gates

The original local proposal records 124 synthetic admission/privacy checks and
87 static workflow/source-contract checks, plus shell and Python parsing.
Those checks used stubs and synthetic logs. They were not rerun for this
publication and do not execute Chibi or establish independent admission.
Original proposal manifest SHA-256:
`0f795d403f0190b40794904323bc634256091818d1f01024f12aa27d05766191`.

For updated transport head `e165059d1c17ec7605d909ae3bbf727d9e267a80`,
[run 39143405107](https://github.com/waldnercharles/chibi-studio/actions/runs/39143405107)
configured and built successfully, then failed the strict product-link audit.
All seven runtime selectors were skipped. The failed audit has no proven root
cause in this checkpoint. A successful build is not a transport runtime pass.

The four complete reader proposal selectors all remain unrun:

1. `language-package-snapshot-chibi`: 16 outer cases.
2. `compiler-artifact`: 15 outer cases plus the existing two captured producer
   cases. The expected outer summary remains 15.
3. `language-signature-reader-chibi`: all four existing reader cases.
4. `language-snapshot-reader-chibi`: all three composition cases.

Each retains one unchanged production 60-second supervisor, including fixture
preparation and cleanup. No split, cap increase, subset, direct-dispatch bypass
or retry is introduced. No assertions, guards, selector registration, source
workloads or product flags change in this checkpoint.

The proposed ordinary build uses Ubuntu 24.04, Clang 18, Debug, compiler ON,
native/desktop OFF, SLJIT ON, two build jobs, and the existing `test_language`,
`test_tools` and `chibi-compiler-host` targets. The unchanged strict audit must
pass before runtime. Raw logs, source, generated metadata, inventories and
binaries remain private and are not included in this handoff.

## Reader contract and resource bounds

The lasting `analysis_state` adapter converts retained snapshot spans to the
existing raw reader tree without reparsing. It preserves fields, named/extra
flags, text, byte positions and negative links, rebases nonnegative links and
the explicit syntax root, and supplies package-local/global maps in graph
member order. It adds no grammar, semantic native callback or compiler result.

The fourteen-source closure is 129,840 UTF-8 bytes, strictly below 131,072;
1,231 more bytes would still remain below that exclusive cap. `State.chibi` is
4,130 bytes, 4,131 including the C terminator, below its existing 8,192 limit.
Three requests use 3, 3 and 2 files, each below 256 bytes. Across BYTECODE and
SLJIT the proposal performs 64 top-level authored calls, 49 parses and four
private JSON decodes. Supplied budgets remain 3,000,000 for snapshot builds and
500,000 for maps/views/readers; these are not aggregate instruction ceilings.

The unchanged bounds are 128 nodes per document, 384 forest nodes, depth 32,
two packages, one import, at most two local documents and three map entries.
Accepted trees compare complete adapted/direct authored results and the C
oracle; rejected prefixes compare trees only. Snapshot, view and reading
retirement assertions remain intact. Runtime maxima and elapsed time are
unmeasured. The detailed resource manifest remains tied to the original source
and must be recomputed after any later source integration.

## Reusing the archived checks

The checker programs intentionally retain their original relative paths and
assertions. They are not self-contained in this documentation directory.
Before any separately authorized synthetic rerun, reconstruct a disposable
packet from the identities in `proposal-manifest.json`: reader inputs under
`source/`, the exact helper under `proposal/reader-state-validation.sh`, the
listed `reference-*` files, and the original sibling transport input directory.
Verify every Git blob and SHA-256 first. Do not replace historical pins with
newer source or infer a fresh result from the recorded counts.

Before runtime or landing, integrate and review the updated transport/semantic
dependency, resolve the strict audit failure, independently admit the exact
source/resource/helper/control combination, refresh immutable source and
control pins, and obtain actual build/audit/complete-selector results. Exact
integration review, server checks and explicit holds still apply. Keep this PR
draft and retain its branch until those requirements are satisfied.
