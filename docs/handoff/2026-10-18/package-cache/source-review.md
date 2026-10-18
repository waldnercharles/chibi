# Independent source and workload review

Date: 2026-10-18. Result: **conditionally admitted for a first complete `language-package-imports` measurement** after exact-source integration, a successful separately admitted build, and verification of the unchanged runner and build controls. No remaining source-review blocker was found in the revised candidate. This is not a runtime pass, publication approval, or permission to execute in this task.

## Exact reviewed identities

- Repository/base: `waldnercharles/chibi`, `codex/toolchain-cleanup-20261017`, `ace559c9c294a450c95a3a730dcd6d9f1b5ce13c`.
- Base test Git blob: `d240ca0b0ef302ece8536c8ad2a430daa927a04b`.
- Revised candidate Git blob: `5420a82cca5bea1311e89deb0f044b76f36e3a72`.
- Revised candidate SHA-256: `d9cbdda7356d84eb244ea275462a0221596c0764b6420b6c7817f767599cbfc5`.
- Revised patch SHA-256: `1f9ef589275efe1c600fac889e7f03d78e5195370f205047c3233ffd87526d23`.
- Exact-base `AGENTS.md` independently fetched/read: Git blob `8ada0dce2353a4954732f2ecd5126981aab27306`. The stored upstream file listing contains no deeper AGENTS file.

The complete candidate and base diff, findings, validation plan, and all manifest identities were inspected. All 32 current manifest files match their byte counts, SHA-256 and Git blob identities. Independent `git apply --check --whitespace=error` passed against the exact base. Diff remains one test file, 129 additions and no removals. No product C compilation, preparation, runtime, mutation test, remote write, saved environment, desktop or browser operation occurred. Only this review report was written by this reviewer.

## Findings resolved during review

1. The original rename from `math/extra.chibi` to `math/renamed.chibi` moved the sibling across `main.chibi` in canonical order. Text-order comparison would therefore still invalidate if path/key comparison were disabled, defeating the proposed path-only sensitivity check. The revised candidate uses `math/extra-renamed.chibi`; both names sort before `main.chibi`, preserving document contents/order while changing path/key. The exact revised bytes above were re-reviewed. Original candidate `de8001e5ee55192b15b57e7074854d9dacd1e73686ea52b1f9bf352060fefa24` and patch `af98608ecfcb657fd5e64f7dcac16e5c5c588acd03dadeaa01d371c0005de5b1` are superseded.
2. The workload now correctly labels 112 as **snapshot** import declarations. One additional direct-source import is parsed once and supplied to nine semantic-only analyses. AGENTS provenance was also added to the findings/manifest.

## Correctness and lifetime review

- `prepare_catalog` copies fixture inputs, supplies actual string lengths, and replaces only the target slot. Every new call uses a different, live previous slot. Fixture arrays and strings remain valid through snapshot construction, which owns its bytes.
- All new generation equality/inequality comparisons occur while the compared owners are live. Failed and prior valid generations cannot have allocator-address reuse during those comparisons. The saved `old` view is not dereferenced after its owner is freed; the function ID is copied and the executable module explicitly retained first. Success and early-failure cleanup use the existing global ownership contract. Backend iterations reset the renamed path and safely replace surviving slots.
- Existing production code requires valid previous generations, compares canonical document path/key/text and imports, and retains sorted direct dependency generations. It includes blank-import graph edges. Accordingly, the repeated semantic failure must rebuild math, middle, app and blank while reusing other; repair from the failed owner has the same reuse count.
- Nonpublication assertions are justified for this fixture's semantic-invalid leaf and propagated dependency failures: preparation stops before exports/module creation. They should not be generalized to every possible backend/finish failure.
- Reversal checks reuse across four singleton packages. Addition/removal changes math membership, while the revised rename isolates path/key identity with unchanged membership, contents and canonical ordering. Each change checks math plus both dependent ancestors and the unchanged unrelated generation. The added function executes as 43, removal checks its absence, and the final app call occurs after prior owners retire.
- Expected arithmetic is consistent: original chain yields 11, edited app 15, edited leaf plus app 19, and Extra(3) yields 43. Retained-old and recovered modules are called after their collection owners have retired. Real-backend identity and fallback counters remain enforced by the unchanged call helper.

Additional exact-base connector reads verified `test/language/main.c` (blob `f80d61f947f32d814e56bf4bd4b6d4dd83107ef4`) dispatches the complete existing owner, and `src/execution/module.c` (blob `c1f597783b1c047446d991e6e1112b123b36f741`) uses reference-counted module destruction, including imported modules. These read-only supplemental sources are not candidate changes.

## Complete-selector workload and admission conditions

The 12 registered cases independently reconcile to 59 preparations, 209 snapshot source parses, 112 snapshot import declarations, 189 package visits before reuse and 48 direct authored calls. Add one direct parse/import, nine semantic-only analyses and one explicit catalog snapshot. Delta from the original selector is 14 preparations, 64 snapshot parses, 32 snapshot imports, 60 visits and 14 direct calls. The new source is exactly 70 bytes.

The fixed workload preserves at most four collection owners, five files per snapshot, source texts below 1 KiB, and one separately retained module handle. Dependency modules/generations are additional retained objects, not extra collection owners. The instruction allowance of 2,000 is per activation, not an aggregate nested-call cap. No filesystem test activity, child test process, GPU, network, service execution or sleep is introduced.

Admitted next measurement, only after the integration/build gates above:

`CHIBI_LANGUAGE_BUILD_DIR=<exact-reviewed-build> python3 -I -B tools/test_policy.py tool language-package-imports`

- Use the complete existing native compiler-enabled language driver with assertions, real SLJIT and the applicable sanitizer configuration preserved. Verify actual configuration/link inputs, not only labels; CMake retains `-UNDEBUG` for the test driver.
- Keep configuration/build timing separate. Use the unchanged production supervisor and its single 60-second deadline for the complete selector. No subset, timeout retry, deadline increase, fake backend, sanitizer waiver, aggregate reconstruction or alternate executable is admitted.
- Recheck final candidate/integration identities, route dispatch and required independent approvals before execution. Any changed product/test bytes need review of the resulting exact combination. Record actual pico totals, timing and exit status; do not infer them from static counts.
- Coordinate the shared documentation update, including the corrected snapshot-import distinction. Preserve PR #407's explicit hold and exclude #420 and their code/dependencies. No publication, merge, broader aggregate, Wasm or production-bug-fix claim follows from this review.

No mutation was executed. The optional mutation hypotheses remain separate future workloads requiring their own admission. The absence of a claimed production bug means no red-before production-fix evidence is asserted.
