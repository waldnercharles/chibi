# CI: one workflow, one ordinary PR job

`.github/workflows/ci.yml` is the only workflow. An ordinary PR runs one Linux
job, **Engine build and native tests**. It checks the actual synthetic merge
checkout, not just the branch head. There are no label-triggered runs, release
jobs, or automatic post-merge platform fanouts. The former seven-job ordinary
compiler PR path becomes one executed job; full mode has ten platform-expanded
jobs. The first consolidated full-Linux run took **36m14s**, including 31m17s of
core validation and 4m20s of native proofs; fewer jobs alone did not make iteration
fast. The path profiles below remove broad legacy work from ordinary native-only
changes. Their actual hosted latency must be measured, not inferred from job count.
Nightly validation is broader; platform-only failures can now be found later
than on an explicitly full-validated PR.

## Ordinary PR and main validation

The job classifies the actual synthetic checkout against its first parent with a
bounded, NUL-delimited diff. A closed allowlist, not a filename-extension guess,
selects the required acceptance profile. Missing or uncertain evidence selects
the full Linux profile. All profiles run formatting and static/infrastructure
checks; each successful PR records its actual profile in the tested-tree receipt.
The PR profile named `full` means Linux core plus native acceptance. It is not
the scheduled/manual `mode=full`, which additionally runs SDK/VM comparison and
the platform, sanitizer, recovery and clean-machine jobs listed below.

| Profile | Eligible changes | Required acceptance |
| --- | --- | --- |
| `static` | Only `docs/**.md` and root `README.md`, except the two runtime/SDK inputs below | Formatting, empty-operand/selector checks and CI infrastructure regressions; no native installation/build |
| `native` | Production files in the 19 registered native compiler modules, `packages/core/strings/Strings.chibi`, and the three existing native diagnostic/attribute fixtures, optionally with docs | Checked pinned build/compiler hosts, VM/native seeds and third-party metadata; native fixed point, native/VM driver and graph command parity, diagnostic coordinates and test attributes |
| `full` | C/host/runtime/ABI/bootstrap/build/CI/registration changes, unknown paths/modules/tests, or uncertain diff | Existing Linux core functional suite followed by the same native proof |

The exact allowlist is `.github/ci-policy.js`, checked against the native driver's
actual imports. New production files in those known modules are compiled by its
existing package-file scan; new modules and test files do not qualify. Core
OS/target/bindings and compiler ABI/prepared/bootstrap paths are not native-only.
`native_driver/Catalog.chibi`, `native_link/Start.chibi` and
`package_compiler/Compile.chibi` also require full validation. So do
`docs/native-compiler-census.md` and `docs/distribution-licensing.md`, which runtime
diagnostics and SDK packaging consume. Paths cannot detect a semantic
ABI change inside an otherwise allowed emitter: reviewers must request full
validation for that case. Native acceptance deliberately does not run the broad
VM-based compiler/project suite; scheduled/manual-full validation retains it. That is a
coverage-frequency tradeoff, not a claim that the native proof covers every
legacy behavior.

Native-only acceptance does not build the C engine or C test drivers. Existing
fetchers check every pinned asset, and the archive preparer retains hash, race
and pruning checks. Real toolchain discovery remains installed because graph
command parity requires it. These are still C-built bootstrap hosts, not a
C-free bootstrap claim.

The full Linux profile builds the Release planning/compiler helpers once and the
selected Debug products/test drivers once. Native self-host receives those same
hosts without another Release build. The retained core suite includes:

- clang-format 22.1.0, empty-operand rejection and test-selector/driver consistency;
- graph/driver programs, tooling/process integrations and case timing checks;
- the existing `native`, `bootstrap-load`, `projects` and `language` groups,
  their assertions, selector deadlines, exclusivity and worker limits;
- native scalar execution and cross-target object generation;
- native compiler fixed point, native driver/VM command parity **and native
  graph/VM compile/link command parity**, diagnostics and test attributes.

In the full Linux profile, `graph-c` and `build-timings` run once through `native`,
rather than also through separate steps. `--skip-vm-comparison` omits only the duplicate VM compiler
self-build timing comparison on ordinary PRs. Unlike legacy `--skip-vm`, it
**does not omit graph parity**. Scheduled/manual-full validation still runs that comparison.

Core validation keeps its 45-minute budget, including checkout, receipt lookup,
formatting, installation and cache restore. Formatting retains its own five-minute
cap. The full Linux profile reserves all three two-minute cache caps before
starting; native-only reserves one two-minute recovered-compiler cache cap;
static restores none. Native bootstrap preparation and remaining core commands
use that same absolute deadline. Native
proofs retain a separate 90-minute budget; diagnostics and attributes each retain
five-minute caps. The single job's 135-minute ceiling is the sum of the old
45/90-minute jobs, not a larger allowance for either phase. Deadline regressions
exercise expiration, status propagation and killing nested process groups.

A main push only skips expensive replay when the receipt guard establishes a
merged PR with the exact successfully tested checkout tree, expected head/base,
independently recomputed profile and workflow, and successful current-attempt job
**and that profile's required steps**. A static receipt cannot certify native/full
acceptance, and a native receipt cannot certify full acceptance. A run's `head_sha` alone is insufficient: PR checkout normally tests a
synthetic merge. Missing, stale, failed, skipped, expired, malformed or inaccessible
evidence means validation runs. Pushes without that exact merged-PR proof also
validate. This is not
a substitute for independent source review or exact-head merge coordination.

## Less frequent modes

| Trigger/mode | Work | Why it is not another ordinary PR job |
| --- | --- | --- |
| Daily schedule, 08:00 UTC | Full suite with ASan + UBSan on Linux/macOS; platform modules; legacy recovery; clean build; SDK; no-C proof | Ownership/instrumentation, actual platforms and clean-machine assumptions remain checked without repeating each PR |
| Manual `mode=ci` | Full Linux profile, fast Debug | Validate an explicitly chosen ref conservatively without a PR diff |
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
| Separate Graph SDK PR job | Scheduled/manual-full Linux native phase, reusing its Release host; SDK check retains its 20-minute cap and source-clean checks |
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

PRs restore only. Main pushes also restore only. Compiler objects, the asset
compiler and recovered compilers retain their three separate path/key stores,
so existing trusted snapshots remain usable. Bundling those paths would change
the cache version and discard that compatibility even with the old key prefixes.
Linux cache publication requires
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
