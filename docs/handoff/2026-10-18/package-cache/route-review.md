# Independent package-cache validation-route review

Reviewed 2026-10-18, approximately 16:03 UTC.

## Decision

APPROVE the frozen local route design, conditional on independent review of the final combined source/helper/docs/supervisor-comment tree and the released, exact public control. No implementation blocker was found in these reviewed bytes. This is not permission to execute, publish, advance a ref, claim a control lease, merge either repository, or claim any runtime pass. Keep the proposal unarmed until those remaining admission gates are satisfied.

## Exact reviewed inputs

- Packet: `../package-cache-validation/`.
- Frozen proposal manifest SHA-256: `2f8cd0ac3e16f39cac918e378a177b578420be4031043b4ea84f2985dfcf7f34`.
- Private helper SHA-256: `5cb53825c0e8e525a2ad0bea8f0578fac5533bfde3cfc8484d7db926a1e0be74`; Git blob `22974029824c5cf1d4548bf8402535964b9fa3c4`.
- Unarmed public workflow SHA-256: `9fbdc39d56c9a2cd878d668f621805633931c865253d0fe89438f071678317cf`; Git blob `08bf571cd59468ce9ff68ca7a6220e8fa0cc6dda`.
- Separately source-reviewed test SHA-256: `d9cbdda7356d84eb244ea275462a0221596c0764b6420b6c7817f767599cbfc5`; Git blob `5420a82cca5bea1311e89deb0f044b76f36e3a72`.
- Conditional source-review SHA-256: `fca56408a1ecf8326aa5cdf6c1afe6f23fcdea7ae89241c63642eab9cc6bd86d`.
- Referenced source integration target: `c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`, tree `09d520fe2a5723226126b0879faaee390e87b7b9`.
- Referenced public control: `15e3875cc1b341a762301ebb1ca26956dea226f4`, tree `571a199a13dcd6dead48357b5838bffbd31edb9c`; base `bee5b54e159cce44ee52ca9439be587d9a95f5d2`.

All 27 manifest-listed files independently match their byte counts, SHA-256 and Git blob identities. The entire frozen manifest remained unchanged after the synthetic rechecks.

## Route and build findings

The existing standalone `test/language` CMake project and normal `test_language` target are sufficient for this test-only cache-regression change. The target retains its complete ordinary translation-unit set; selecting package imports changes execution only. Compiler ON, native SDK OFF and desktop OFF exclude the engine/desktop configuration while retaining real native execution and real SLJIT. No replacement dispatcher, fake backend, alternate build system, test_tools, reflect, render, installed-product build, compiler-host build or transport-boundary audit is needed for this scoped route.

The helper explicitly requests Debug Clang 18, ASan/UBSan in compile and executable-link flags, frame pointers, assertions, real SLJIT and parallelism 2. All five official source dependency pins match the reviewed semantic helper, and CMake/Ninja installation remains hash-pinned through the existing requirements file. Configuration/build are separate timed phases.

The private verifier fails closed unless actual generated cache values match the recipe, all generated compile commands carry both sanitizer/frame-pointer flags without sanitizer/assertion/backend disabling overrides, the dispatcher and changed owner carry `-UNDEBUG`, the execution JIT has its real enable definition, and the patched real SLJIT translation unit is present. It checks the actual Ninja link command for sanitizer flags and the SLJIT archive, then checks linked ASan, UBSan and SLJIT code-generation symbols. Binary/cache/compile-database/Ninja-file hashes remain private, are checked before execution and again in the final verdict. This is appropriate evidence to require from the eventual build; no real generated metadata, binary or instrumentation pass was observed in this review.

Only the existing complete `language-package-imports` production-supervisor route executes, once. The selector stays outside the aggregate. There is no direct dispatcher bypass, subset, retry, enlarged test allowance or sanitizer waiver. The unchanged supervisor's absolute deadline, process ownership and cleanup functions remain bytecode-AST-identical to the source-review base. A two-minute Actions outer guard does not replace its single 60-second ceiling.

The classifier requires exit zero, exactly one successful 12-case pico summary with nonzero assertions, exactly one correctly formed production-supervisor report at or below 60 seconds, no failure/sanitizer marker and verified source identity. The verdict additionally requires exact phase/selector sets, zero phase exits and unchanged sealed build inputs. Missing, failed, duplicate, timed-out, malformed or inconsistent evidence cannot pass. It deliberately does not guess an assertion count.

## Workload carried forward

The separately reviewed complete candidate has 12 cases, 59 preparations, 209 snapshot parses plus one direct parse, 112 snapshot imports plus one direct import, 189 preparation-loop package visits before reuse, 48 direct authored calls, nine semantic-only analyses and one explicit catalog snapshot. Bounds remain four collection owners, five files per snapshot, each source below 1 KiB, one additional retained module handle and a 70-byte new fixed source. The unchanged 2,000 instruction allowance is per activation, not an aggregate nested-call cap. No filesystem test work, child test process, network, GPU, engine frames, service execution or sleeps are introduced. Runtime elapsed time and actual pico assertions remain unmeasured.

The final source-owned docs and comment-only supervisor accounting must carry these complete totals and the snapshot/direct distinction. This route approval does not independently approve a later integration conflict resolution or changed test/product source.

## Full workflow inventory and collateral admission

I independently read the complete immutable Studio workflow directory and the complete base-to-control changed-path list through the GitHub connector:

- `ci.yml`: Git blob `c7b98fd05c90dfb653ac7316648adfb15db4c8f8`.
- `publish.yml`: Git blob `a76a11688f8e278c598b3dfeb332f629f63d5fd5`.
- Existing replacement path `toolchain-m5-unit-analysis.yml`: Git blob `2532931f208cd8246716046142372449d08c505f`.

There are no other workflow files at this referenced head. The exact same-repository PR81 branch exclusion disables ordinary Studio CI's sole job. All four existing PR changed paths avoid the publish workflow's positive PR path filters: `.github/scripts/toolchain-m5-validation.sh`, `.github/toolchain-m5-admission.json`, `.github/workflows/ci.yml`, and `.github/workflows/toolchain-m5-unit-analysis.yml`. Replacing only the last path preserves that isolation. Existing inert public helper/admission files do not execute in the proposed route. Push triggers elsewhere are main-only. Manual/scheduled workflows are not caused by this PR update and are not admitted here.

I also independently read the complete source workflow inventory at `c828177e`: `ci.yml` (`a8b553c3d1540d32f8e0f868b65b97005d0375e4`), `publish.yml` (`4a7ed533c0d399cf94b49f824f64d8f083ea9b0b`), and `source-bundle.yml` (`4983b9fbed49b63aba4e3b4bcff30b74228a63ed`). Their push triggers select main; CI/source-bundle PR triggers also select main, and publish has no PR trigger. Accordingly, staging the proposed feature branch and targeting `codex/toolchain-cleanup-20261017` does not trigger these collateral workflows. Changing the source PR base to main would require renewed collateral-workflow review.

These are exact referenced-tree findings, not an assertion about the future released parent. Native/transport work may change PR81 first. Final admission must re-fetch the actual released head/base, complete workflow inventory and complete PR changed paths, prove the sole intended replacement, and preserve the CI guard and publish exclusions.

## Privacy, identity and attempt controls

Source checkout uses the existing authorized token and branch with shallow/fresh checkout, no submodules/LFS or persisted credentials. Debug logging is rejected before either checkout. A trusted public gate verifies immutable clean private HEAD/tree before invoking the private helper. The helper checks clean source/control identities and exact branch/base/repository admission before material phases. The public workflow retains only contents:read, adds no deployment, settings change or persistent access grant, and uses no saved cloud or desktop environment.

Helper diagnostics, dependency output, generated metadata, symbol inventories, binaries and hashes remain runner-local. Phase children lose the public output descriptor. Only fixed labels, integer timing/exit values, source/control commit/tree identities and evidence booleans are projected to one status.json; the upload path is that exact file. No raw-log/source/history/build artifact, source bundle or cache upload is added.

GitHub attempt must be 1, and the local selector marker rejects a second execution within the job. Those guards are not cross-run deduplication: a new synchronize/reopened event creates a fresh run. The coordinator must not retrigger an unchanged candidate to retry a failed measurement without separate admission. This packet claims no lease on the shared control and must remain unarmed while other lanes own it.

## Checks and remaining gates

Independently reran the supplied 147 synthetic helper/admission/privacy/configuration/classification checks and 77 static workflow/source checks. All 224 passed, including Bash and embedded Python syntax. Added 19 independent immutable-inventory, collateral-trigger, complete-public-delta, dependency-pin and unchanged-supervisor checks; all passed. Synthetic verification uses mocked Git/Ninja/nm and nonexecutable placeholder text, with configuration/compilation/product execution blocked by stubs. No product command, actual C preparation/execution, remote mutation or public export occurred.

Remaining mandatory gates:

1. Refresh the active source target and holds; integrate the exact source-owned test plus narrowly coordinated docs/supervisor comments and this helper. Preserve PR407's hold and exclude PR420 and their code/dependencies.
2. Independently review the full clean combined source tree, changed contracts and build dependencies. Resolve its immutable helper-bearing commit/tree. Confirm production cache code and supervisor executable functions remain unchanged for this scoped regression.
3. Obtain coordinator release of existing PR81. Recheck its real parent/base and full workflow/delta isolation. Review exact final pin substitutions and arming separately; invalid source pins and `PACKAGE_CACHE_REVIEWED: false` currently fail closed.
4. Separately admit one configuration/build/verification and one complete first selector measurement at that exact source/control combination. Failures are diagnostic evidence, not permission to repeat, narrow or relax the route.
5. Before landing, recheck exact tested integration, current required server checks/reviews and all explicit holds. Retain branches. Do not merge the temporary public control or claim aggregate, Wasm, installed-product, stage-one/stage-two or self-hosting completion.

Reviewer scratch is retained only under `review-cache-validation-route/`: this report, synthetic/static reports and read-only workflow inventory/provenance. No generated product binaries or project copies were created.
