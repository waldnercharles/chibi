# CI: one workflow, one ordinary PR job

`.github/workflows/ci.yml` is the only workflow. An ordinary PR runs one Linux
job, **Engine build and native tests**. It checks the actual synthetic merge
checkout, not just the branch head. There are no label-triggered runs, release
jobs, or automatic post-merge platform fanouts. The former seven-job ordinary
compiler PR path becomes one executed job; full mode has ten platform-expanded
jobs. This removes repeated runners/builds, not a claim of measured speedup: one
sequential job can have a longer critical path than the old parallel jobs.
Nightly validation is broader; platform-only failures can now be found later
than on an explicitly full-validated PR.

## Ordinary PR and main validation

The Linux job installs tools once, builds the Release planning/compiler helpers
once, and builds the selected Debug products/test drivers once. Native self-host
then receives those same hosts; it does not enter another Release build.

The suite retains:

- clang-format 22.1.0, empty-operand rejection and test-selector/driver consistency;
- graph/driver programs, tooling/process integrations and case timing checks;
- the existing `native`, `bootstrap-load`, `projects` and `language` groups,
  their assertions, selector deadlines, exclusivity and worker limits;
- native scalar execution and cross-target object generation;
- native compiler fixed point, native driver/VM command parity **and native
  graph/VM compile/link command parity**, diagnostics and test attributes.

`graph-c` and `build-timings` run once through `native`, rather than also through
separate steps. `--skip-vm-comparison` omits only the duplicate VM compiler
self-build timing comparison on ordinary PRs. Unlike legacy `--skip-vm`, it
**does not omit graph parity**. Full validation still runs that comparison.

Core validation keeps its 45-minute budget, including checkout, receipt lookup,
formatting, installation and cache restore. Formatting retains its own five-minute
cap. Cache restore reserves its entire two-minute cap from the core budget before
starting. The remaining core commands use the same absolute deadline. Native
proofs retain a separate 90-minute budget; diagnostics and attributes each retain
five-minute caps. The single job's 135-minute ceiling is the sum of the old
45/90-minute jobs, not a larger allowance for either phase. Deadline regressions
exercise expiration, status propagation and killing nested process groups.

A main push only skips expensive replay when the receipt guard establishes a
merged PR with the exact successfully tested checkout tree, expected head/base,
canonical profile and workflow, and successful current-attempt job **and required
steps**. A run's `head_sha` alone is insufficient: PR checkout normally tests a
synthetic merge. Missing, stale, failed, skipped, expired, malformed or inaccessible
evidence means validation runs. Pushes without that exact merged-PR proof also
validate. This is not
a substitute for independent source review or exact-head merge coordination.

## Less frequent modes

| Trigger/mode | Work | Why it is not another ordinary PR job |
| --- | --- | --- |
| Daily schedule, 08:00 UTC | Full suite with ASan + UBSan on Linux/macOS; platform modules; legacy recovery; clean build; SDK; no-C proof | Ownership/instrumentation, actual platforms and clean-machine assumptions remain checked without repeating each PR |
| Manual `mode=ci` | Canonical Linux, fast Debug | Reproduce the ordinary gate on an explicitly chosen ref |
| Manual `mode=full` | Same full proof families; `sanitizers=true` explicitly opts into ASan + UBSan | Cross-platform/clean-machine validation before a sensitive change or milestone |
| Manual `mode=graph` | Isolated Graph harness, Release host/record identity checks, generated resources, incremental/command checks, SDK relocation and functional suite | Distinct isolation proof, not a second routine functional suite |
| Direct maintenance scripts | Bootstrap/runtime/native seed preparation and optional source bundle | Infrequent asset maintenance is not validation on every source change |

Manual modes never imply authorization to dispatch, publish a release, change
pins, or override a hold. Scheduled ownership checks remain nonadvisory. Failures
must be diagnosed; moving them off PRs does not make them acceptable.

## Retirement map

| Former workflow/job | Replacement |
| --- | --- |
| `ci.yml` Linux engine | Same stable check name; shared core/native job |
| Separate format job | Bounded format/static phase in Linux job |
| Separate Graph SDK PR job | Full Linux native phase, reusing its Release host; SDK check retains its 20-minute cap and source-clean checks |
| macOS engine | Scheduled/manual-full, same 45-minute limit and instrumented profile |
| macOS/Windows native modules | Scheduled/manual-full, real runners, same 10-minute limit; consumes Linux bundle |
| Clean default build | Scheduled/manual-full, separate clean checkout, same 45-minute cap, tooling census and six-product assertions |
| `native-self-host.yml` | Canonical Linux native phase; bounded seeded recovery is unchanged |
| `compiler-recovery.yml` | Scheduled/manual-full, three isolated 30-minute platform jobs plus 5-minute comparison; compiler bytes and scalar evidence remain checked |
| `no-c-compiler.yml` | Scheduled/manual-full, same 90-minute clean-machine proof and vendor sample |
| `linux-graph.yml` | Manual `graph` mode, same 45-minute limit and distinct host/harness identity checks |
| `bootstrap-host.yml`, `compiler-runtime-seed.yml`, `native-seed.yml` | [Manual preparation and publication procedure](ci-maintenance.md); no automatic release/pin writer |
| `source-bundle.yml` | On-demand source-bundle script, no automatic packaging on main |
| `ci-control.yml` | Retired. Use native Actions rerun/dispatch controls with ordinary repository permissions; no label-driven privileged automation |

Legacy prepared/VM recovery remains necessary because pinned C-built hosts and
prepared artifacts still have active consumers. The no-C job removes C compilers
from the validation machine but consumes prebuilt C-built hosts and third-party
archives; it is **not** a fully C-free bootstrap.

## Caches and evidence

PRs restore only. Main pushes also restore only. Linux cache publication requires
a successful Linux full-mode native/SDK proof on trusted `main`, scheduled or
manually selected. Compiler content/toolchain/profile identities remain in keys
and in the underlying content-keyed stores. Unrelated platform failures do not
invalidate those Linux cache entries, and cache publication is **not** evidence
that the whole full matrix passed. macOS retains its platform-qualified cache
policy on trusted full-main runs. Clean recovery/no-C/default-build jobs do not
restore these stores. No untrusted PR artifact is promoted into a trusted cache.

Logs, timings, profile evidence and recovery evidence remain downloadable.
Profile-symbol evidence retains its existing informational status; functional,
fixed-point and ownership assertions are not downgraded.

## Outstanding platform gate

Current main does not yet contain Windows native self-build. PR #1315 must add
its real cross-link/Windows fixture/stage-3-equals-stage-4 proof as an explicit
**non-sanitizer Windows-only manual mode in this same workflow**. Its existing
90/60-minute bounds and exact-branch fixed-point requirement remain; the prior
setup timeout is not a runtime pass. This consolidation neither imports that
unverified feature nor replaces it with a skipped/green placeholder. Existing
Windows prepared recovery and real Windows target-module execution remain in
full mode. The known prepared-target-ABI failure is not waived.
