# Live Outliner checkpoint

This is the user-requested commit-and-push checkpoint of paused work, so it can
continue in another workspace. It is not a merge-ready release. No implementation
work or expensive validation was resumed to publish this checkpoint.

## Check out the pair

```sh
git clone --branch integration/outliner-m3-20261018 https://github.com/waldnercharles/chibi.git chibi
git clone --branch codex/live-outliner-20261018 https://github.com/waldnercharles/chibi-studio.git chibi-studio
```

The companion Studio handoff pins the engine checkpoint commit. Keep both
repositories together and build Studio against an installed SDK from this engine
revision. Build outputs and SDK binaries are not committed; rebuild them on the
new host. Absolute `/workspace/...` paths in the historical evidence describe
the original validation environment and must be adapted.

The engine checkpoint preserves the pending merge's parents:

- Language-redesign parent:
  `ff6d06c888fe660894776255ec7f9fc507db6393`.
- Published toolchain M3: `26e0821419e753f42041383bbe4f25db77b62121`.
- Studio source base: `95ca95bc3ca2da5b899ec06596982ade26d2d94f`.

M4 was deliberately excluded. Do not rebase onto the toolchain branch's latest
tip without first coordinating its scope. The private exact-number core remains
inactive in the frontend; no new cast, promotion or default numeric policy was
chosen.

## Implemented

Studio captures owned scene facts, executes row projection in `.chibi`, then
publishes a complete owned model through guarded native services. Filtering,
paging, counts and all twelve row fields are authored. Publication checks the
actual runtime context, request, source module, mount, scene and session. Queued
events, deferred reveal, cancellation, reload and provider lifetimes are covered
by focused fixtures and independent source review.

Two engine changes unblock large authored projections without new syntax:

- Proven private plain-I32 append sequences grow a reusable tail while preserving
  immutable prefixes and snapshots, removing quadratic array copying.
- Refresh and Reveal use a shared request work account. Capture grants one checked
  allowance derived from captured facts; helpers, queues and continuations cannot
  renew it. Ordinary function budgets remain unchanged. The qualified canonical
  coefficient is K=249, pinned to the shipped sources and preparation ABI.

Exhaustion before Publish retains the previous valid model. Exhaustion after an
accepted Publish retains the newly committed valid model. Fuel bounds authored
checkpoints, not all native work or wall time; arbitrary edited policies may
still exhaust. Native byte-at text scanning remains a possible performance
follow-up. New syntax still requires the owner's review before implementation.

## Verified before the pause

All test invocations retain the production 60-second supervisor. Configuration,
build and package times are separate. Native correctness and Studio checks below
used Debug ASan/UBSan with default leak checking; Wasm used actual generated
entries plus bytecode. Software Vulkan was Mesa llvmpipe, not hardware timing.

| Check | Result |
| --- | --- |
| Native / materials / language groups | Passed in 32.834 / 34.176 / 25.646 seconds |
| Fifteen focused M3, authority, publication and SDK ownership checks | Passed; individual logs retained |
| Private append | Native 6 cases / 1,717 assertions; serial Wasm 6 / 1,716 |
| Request fuel | Native and serial Wasm 5 / 815 each; generated C 1 / 107 |
| Canonical work census | 1 / 393; K=249; actual Project derives two append plans |
| Mounted live fuel | 1 / 10,184, both bytecode and SLJIT; 4,096-actor Refresh and before/after-Publish exhaustion |
| Studio arithmetic / live / guards | 4 / 204; all twelve live scenarios; guards passed |
| Studio projection / helper / scale | Passed, including all row fields and ownership |
| Two-frame native startup | Passed in 15.814 seconds; clean full sanitizer log and manually inspected 1920x1080 PNG |

SDK10 package ID was
`778b78b7b14216ea6f3445b4b3ff23abe0efa609e7ee9d717b88c03c2a52fa27`,
with source snapshot
`8cf9fcb246917d70fbd688b891ba6880378078cba5a5668a4ff989ffeb96abdf`.
All 1,562 SDK and 195 Studio compile entries were sanitizer-instrumented. Studio
was explicitly reconfigured after atomic SDK promotion; app and acceptance were
relinked. The tests exercised the then-uncommitted source, not the later
checkpoint commit metadata. Saved source manifests identify those exact files.
The final two Studio fixture edits only corrected comments about work budgets.

The fixed 4,096-actor scale diagnostic reduced logical value-arena use from
137,727,624 to 2,788,966 bytes, with 589,760 packed-copy bytes. This is allocation
evidence, not a comparable Release or UI latency benchmark. Its diagnostic
larger direct budget is separate from the passing production request-fuel test.

Earlier failures are preserved, including scalar eligibility/copying failures,
request fixture lifetime/yield corrections, the live-fuel transport-boolean
oracle correction, a language aggregate timeout and the prior Cute startup
UBSan failures. The reviewed Cute fixes remove the startup diagnostics without
changing the screenshot pixels. Missing or disabled CI is not a passing check.

## Resume checklist

1. Read both AGENTS.md files and the archived independent reviews. Verify current
   upstream state and coordinate with the owners of the language redesign and
   toolchain work before integrating newer changes.
2. Rebuild the applicable native, installed SDK and Studio artifacts. Explicitly
   configure Studio after SDK promotion; timestamps alone do not establish a
   matching consumer. The Release controller build had passed and runtime-only
   tools reached a completed build before the pause, but their final test groups
   had not run.
3. Finish the worker language build (`WEB_THREADS=1`) and run `wasm-wasm`,
   `wasm-scalar-append` and `wasm-request-work`. Its build was intentionally
   interrupted at the pause. Run the final serial Wasm aggregate as well.
4. Run the remaining projects and tooling groups, plus the selected build-process,
   build-toolchain and CLI checks. Keep each original shared 60-second deadline.
5. Qualify a relocated installed SDK consumer with the four selected core,
   imports, record-publication and headless checks. Exact configuration guidance
   is in the archived integration review; this consumer was not built yet.
6. Build final serial web SDK and Studio; run the admitted 16-frame browser
   fixture and manually review its three screenshots.
7. Use a separate ordinary Debug, assertions-on render profile for the admitted
   Outliner move (21 frames) and scene move (2 frames) checks. Preserve their
   64-MiB binary preflight; an approved debug-only stripped copy must retain
   identical loadable sections. The sanitizer build cannot meet that size cap.
8. Reconcile dependencies, finish the exact-candidate validation and independent
   review, then reassess merge eligibility. Full native/render/Studio suites
   remain quarantined and must not be reconstructed from smaller runs.

At the last pre-pause remote check, engine main was `6ea2221c`, Studio main was
`bee5b54e`, engine PR420 was open at `ff6d06c8`, and Studio PR71 was a draft at
`62962adc`. That owner was repairing separate prototype source ownership with
engine PR439. This checkpoint uses the older Studio base above; it does not
include or replace that newer work. These are historical observations, not a
fresh merge-eligibility check.

Preserve engine PR407's hold, exclude validation-only Studio PR78/79/80, and
leave the other worker's PR420/PR71/PR77 landing sequence under that worker's
ownership. Retain branches; no force pushes, bypassed checks, weakened assertions,
permission/protection/billing/credential changes or deployment are authorized.
Existing publication workflows previously hit an account spending gate; no
artifact-publication success is claimed.

## Portable evidence

[Evidence directory](research/outliner-checkpoint-20261018/) contains the final
runtime, integration and Studio source reviews, the validation plan, exact source
manifests, selected passing and failing logs, census/allowance reasoning, and
startup screenshot/provenance. Logs are losslessly compressed as `*.log.gz`;
`ORIGINAL_LOG_SHA256` records their original byte identities. `SHA256SUMS`
covers the stored evidence.
Historical notes are preserved verbatim and may describe an earlier pending
state; this handoff and the latest named logs identify the pause checkpoint.
