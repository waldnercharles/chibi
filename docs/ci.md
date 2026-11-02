# CI: one workflow, explicit platform validation

`.github/workflows/ci.yml` is the only workflow. Ordinary PRs, pushes to `main`,
and manual `mode=ci` run five jobs:

1. **Engine build and native tests**: Linux core behavior and native fixed point.
2. **Engine build and native tests (macOS)**: the existing macOS core suite.
3. **Native scalar modules (macos-arm64)**: link and execute Linux-generated modules on macOS.
4. **Native scalar modules (windows-x64)**: link and execute those modules on Windows.
5. **Native self-host (Windows)**: run the cross-linked compiler, exercise the
   native Windows paths, and verify its bounded native fixed point.

PRs test the actual synthetic merge checkout. Main pushes validate again,
including ordinary merges: there is no changed-path classifier, tested-tree
receipt, or post-merge skip protocol. This intentionally trades repeated merge
validation for simpler, unconditional coverage. Documentation-only changes use
the same checks. Check names remain stable. The Linux job still rejects a changed HEAD or dirty
tracked/untracked source after testing, without retaining a receipt subsystem.

## Reference architecture and adaptations

The reference is Zig **0.15.1**, commit
[`3db960767d12b6214bcf43f1966a037c7a586a12`](https://github.com/ziglang/zig/tree/3db960767d12b6214bcf43f1966a037c7a586a12),
not a claim about today's Codeberg configuration. Its
[workflow](https://github.com/ziglang/zig/blob/3db960767d12b6214bcf43f1966a037c7a586a12/.github/workflows/ci.yaml)
uses explicit platform/configuration jobs on PRs and mainline pushes. Its
[Linux release recipe](https://github.com/ziglang/zig/blob/3db960767d12b6214bcf43f1966a037c7a586a12/ci/x86_64-linux-release.sh)
builds a candidate, runs behavioral suites through it, verifies a byte-identical
self-build, and checks a fresh bootstrap seed. The build graph owns the test
families; CI does not select correctness by changed filenames.

Chibi follows that explicit build/test structure, using its existing scripts,
registered selectors and platform runners. It does **not** copy Zig's runner
fleet, nine-hour Linux ceiling, or local-cache reset policy. Chibi retains its
own deadlines and three content-qualified caches. Clean bootstrap, SDK, no-C,
Graph isolation and sanitizer certification remain scheduled/manual rather than
expanding every PR into the complete matrix. That cadence is a deliberate Chibi
adaptation, not identical Zig coverage.

## Canonical build and tests

Linux builds the Release native CLI and explicit C comparison helpers once,
then the selected Debug products/test drivers. The core suite uses those paths.
Native self-host receives the comparison hosts without another Release build;
the native build entry remains the ordinary route. The native phase then runs
19 direct checks covering the driver, cache, minimum version, formatting,
output, diagnostics, generations, test attributes, failures, Busy, optionals,
build entry, runner, tasks, scalar math, managed values, maps, CLI and reload.
macOS retains its Release-helper/Debug-product recipe and test selection.

Native compilation uses `.github/native-llvm.sh` and `CHIBI_LLVM_BIN` with
LLVM/LLD 22 or newer. Linux C compilation and the libclang bridge retain LLVM 18.

The canonical coverage includes:

- clang-format 22.1.0, empty-operand rejection and selector/driver consistency;
- CI infrastructure regressions and real launcher command-flow fixtures;
- graph/driver programs, tooling/process integrations and case timing checks;
- existing native, project and language selectors, preserving assertions,
  selector deadlines, exclusivity and worker limits;
- native scalar execution and cross-target object generation, then real
  macOS/Windows linking and execution of the generated bundle;
- native compiler fixed point, native/VM driver command parity **and native/VM
  graph compile/link command parity**, diagnostics and test attributes.

The native fixed point remains stage 2 equals stage 3; an older pinned seed may
require exactly one additional stage, which must prove stage 3 equals stage 4.
There is no retry loop or relaxed comparison. VM comparison is opt-in through
`--compare-vm`. Ordinary CI passes `--compare-vm --skip-vm-comparison`; full
proofs pass `--compare-vm`. `--skip-vm-comparison` omits only
the extra VM-hosted compiler comparison on ordinary runs; unlike legacy
`--skip-vm`, it **does not omit graph parity**. Scheduled/manual-full runs retain
that VM comparison.

The VM/C behavioral suites are not replaced by a compiler compiling itself.
The Release native `chibi test` compiles one executable per package and runs
each test in an isolated process. Eligible pure tests use a bounded worker pool;
effectful tests retain their ordering barriers and reports stay in source order.
Native-build and recovery cases deliberately exercise clean outputs,
invalidation and bootstrap paths. Candidate reuse preserves those behaviors.

Linux core validation keeps its 45-minute budget, including checkout,
formatting, installation and all cache restores. Formatting retains its own
five-minute cap; three two-minute cache caps are reserved within the same core
budget. Native proofs retain a separate 90-minute budget; diagnostics and
attributes each retain five-minute caps, as do native generations. The Linux
job's 135-minute ceiling is the sum of the old 45/90-minute jobs, not a larger
allowance for either phase.
macOS core retains 45 minutes; each target-module job retains 10 minutes.
Windows native self-host retains its 60-minute bound and runs without sanitizers.
Watchdog regressions cover expiration, status propagation and nested process
cleanup, including external cancellation.

The prior Linux-only checkpoint at `55cdf9be` completed in **27m26s** with all
362 C compiler-cache requests hitting: core 22m35s and native proof 4m03s.
That is an observed baseline, not a latency claim for this revised platform
workflow. Removing orchestration does not remove the behavioral suites' cost.
The earlier unexplained local 60-second infrastructure timeout is not claimed
fixed by deleting the now-obsolete receipt subsystem.

## Less frequent modes

| Trigger/mode | Additional or distinct work |
| --- | --- |
| Daily schedule, 08:00 UTC | Linux/macOS ASan + UBSan; prepared recovery; clean default build; SDK; no-C proof |
| Manual `mode=ci` | The same five canonical jobs as PR/main, without sanitizers |
| Manual `mode=full` | Canonical jobs plus recovery/clean-build/SDK/no-C; `sanitizers=true` explicitly enables ASan + UBSan |
| Manual `mode=graph` | Isolated Graph harness, Release host/record identity, generated resources, incremental/command checks, SDK relocation and functional suite |
| Direct maintenance scripts | Bootstrap/runtime/native seed preparation and optional source bundle |

Manual modes never imply authorization to dispatch, publish a release, change
pins, or override a hold. Scheduled ownership checks remain nonadvisory.
Failures must be diagnosed; moving them off PRs does not make them acceptable.

## Retirement map

| Former workflow/job | Replacement |
| --- | --- |
| `ci.yml` Linux engine | Stable check name; core/native job on PR/main and canonical manual runs |
| Separate format job | Bounded format/static phase in Linux job |
| Separate Graph SDK PR job | Scheduled/manual-full Linux native phase, reusing its Release host; 20-minute cap and source-clean checks |
| macOS engine | Canonical PR/main/manual job; same 45-minute limit; scheduled/explicit full instrumentation |
| macOS/Windows native modules | Canonical real runners; same 10-minute limits; Linux bundle uploaded on ordinary runs too |
| Clean default build | Scheduled/manual-full, separate clean checkout, 45-minute cap, tooling census, native `chibi`/asset-compiler products and private-host absence assertions |
| `native-self-host.yml` | Canonical Linux native phase plus Windows native object/self-host job; original 90/60-minute bounds |
| `compiler-recovery.yml` | Scheduled/manual-full, three isolated 30-minute platform jobs plus 5-minute comparison; compiler bytes and scalar evidence checked |
| `no-c-compiler.yml` | Scheduled/manual-full, same 90-minute clean-machine proof and vendor sample |
| `linux-graph.yml` | Manual `graph` mode, same 45-minute limit and distinct host/harness identity checks |
| `bootstrap-host.yml`, `compiler-runtime-seed.yml`, `native-seed.yml` | [Manual preparation and publication procedure](ci-maintenance.md); no automatic release/pin writer |
| `source-bundle.yml` | On-demand source-bundle script, no automatic packaging on main |
| `ci-control.yml` | Retired; native Actions controls replace label-driven privileged automation |

Legacy prepared/VM recovery remains necessary because pinned C-built hosts and
prepared artifacts still have active consumers. The no-C job removes C compilers
from its machine and uses the pinned native seed, native build entry and native
vendor build, with assertions that no VM hosts were fetched. Third-party
archives remain; it is **not** a fully C-free bootstrap.

## Caches and evidence

PRs and main pushes restore only. Compiler objects, the asset compiler and
recovered compilers retain their three separate path/key stores, preserving
existing trusted snapshots. Bundling those paths would change the cache version
and discard compatibility even with the old key prefixes.

Linux cache publication requires successful Linux native/SDK validation on
trusted `main`, scheduled or manually selected in full mode. OS, toolchain,
profile and content identities remain checked. Unrelated platform failures do
not invalidate those qualified Linux entries; saving them does not certify the
whole matrix. macOS retains its trusted-full-main-only cache policy. Windows
LLVM cache publication also requires successful trusted full-main validation;
the downloaded archive is checksum-pinned. Clean
recovery/no-C/default-build jobs do not restore these stores. Untrusted PR
artifacts are never promoted into trusted caches.

Logs, timings, native-profile evidence and recovery evidence remain downloadable.
Profile-symbol evidence retains its informational status; functional,
fixed-point and ownership assertions are not downgraded.

## Windows native self-build

The Linux native phase cross-links stage two and uploads it for the real
Windows runner. Windows uses checksum-pinned LLVM 22.1.8, exercises its asset
compiler, object pipeline, build runner, formatting, output, task timing, math
and SDK paths, and builds stages three and four. Their bytes must match; an
older compiler may require the existing single fallback, stage four equals
stage five. This runs on the ordinary canonical cadence, without sanitizers
and within 60 minutes. A manual pinned-artifact input checks producer, source
commit and compiler hash before reuse; it does not waive the fixed point.
The exact integrated head still needs fresh CI. Windows prepared recovery
remains in full mode; the known prepared-target-ABI failure is not waived.
