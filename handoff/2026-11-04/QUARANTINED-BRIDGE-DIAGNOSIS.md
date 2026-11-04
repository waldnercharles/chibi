# Retained 7482 artifact: compatibility evidence, not seed admission

## Result

A retained Linux artifact avoids rebuilding the first bridge just to inspect its compatibility. It does **not** supply an accepted replacement seed or remove the old bootstrap's unresolved terminal state.

The three bounded emit-only probes completed in 4.64 seconds, with all frozen inputs unchanged and no generated output executed:

- Current parser closure: 8 files / 124,561 bytes; emitted successfully in 0.298 seconds.
- Current scalar LLVM closure: 32 files / 1,122,111 bytes; rejected `ValueReach` at `llvm_native/Emit.chibi:2074:13` in 1.439 seconds. This is the same `build^.used.maps.append(type);` rejection seen with published c5.
- Exact 14f7 scalar LLVM closure: 30 files / 970,298 bytes; emitted successfully in 2.841 seconds.

These are source-compatibility observations, not generated-code correctness tests or a performance benchmark. They support the documented intermediate bridge direction, but neither establish complete 14f7 compiler compatibility nor justify a direct jump to current source. Detailed output identities and closure hashes are in [quarantined-7482-results.json](quarantined-7482-results.json).

## Artifact provenance

[Run 41574174752, Linux job 125714037928](https://github.com/waldnercharles/chibi/actions/runs/41574174752/job/125714037928) retains artifact `native-stages-linux`, ID `13204584163`.

- Run head is `7482af7bef75d950ab6e3d9216f8e7078ce70394`; checkout was merge commit `c551ca127bec4eb3dd14f3b936307023be654fa4`.
- Both commits have the exact Git tree `68312d959ad5c6f232ab0530284dedc2c874b2e9`. The merge introduces no source differences.
- Downloaded ZIP: 7,005,539 bytes; SHA-256 `0c7fe2018c3b77085edf3708e99adc28279ab2f606a85f422595bbb2a4f4e5b6`, matching GitHub's artifact digest.
- Both raw stage3/stage4 members: 10,062,736 bytes; SHA-256 `6e867c8e5588509286a5709ecdca902e8aaeb6d3db51f1ea4efb1bb09b677d03`. Their bytes compare equal and hashes match the job log.
- The historical log records stage2/3/4 at 483/72/64 seconds, all exit 0, with the fixed point reported at `2026-11-03T04:35:14Z`.
- The encompassing step and job still failed: the mandatory native-runner VM/dry-run parity contract later failed because an imported package was missing from the VM snapshot. The core-test step also failed. A raw pair is not a substitute for those requirements.

The newer retained PR-head 6b46 and 72598 candidate logs likewise show matching raw stage3/stage4 hashes followed by a runner parity failure. They were not downloaded or admitted in this pass. The latest 100 releases contain no 14f7 native seed; the latest native seed is still c5. An exact-head workflow query for 14f7 returned no runs. The previously reviewed 14f7 package is recorded in the original handoff but is not available in this cloud workspace.

## Containment and limits

The inspected ELF is x86-64 Linux, with the standard system interpreter and `libc.so.6` dependency, and no RPATH/RUNPATH. Inspection used `readelf`, not `ldd`. Exact 7482 source confirms that `--emit-llvm` writes the module bundle and returns before LLVM/linker invocation. Each probe used jobs 2, a 14-second deadline and the existing owned cleanup controller. There were at most three cases, with a 45-second aggregate admission window. Isolated output/cache locations were used; no installed seed, pin, release, product or generated executable was changed or run. The candidate was returned to non-executable mode after completion.

The existing full bootstrap was neither restarted nor duplicated. No later whole-compiler stage was launched, and its unresolved receipt remains a separate blocker. No software installation or saved/user-computer environment was used.

## Admission work still required

1. Preserve the exact producer's failed core and mandatory runner contracts. Diagnose and satisfy the applicable checks rather than accepting a partial producer.
2. Normal acceptance still needs exact-source compiler behavior, complete required tests and raw fixed-point evidence for the target. Tiny emit-only probes do not establish these.
3. Published-c5 admission and supported-platform requirements remain. This Linux diagnostic does not authorize Windows consumption of a partial producer or change any platform gate.
4. Obtain the already reviewed exact 14f7 package through a permitted transfer, or reconcile the original bootstrap owner and terminal outcome before scheduling any replacement bridge build. Do not increase the 90-minute limit as a diagnosis.

## Small source improvement ready for a focused change

Current `package_compiler/Scalar.chibi` calls `AnyReference(params)` twice inside `ScalarEmits`, at lines 4362 and 4433. `ScalarEmits` has one call site, inside the per-function loop at line 4867. The predicate reads only `Param.reference`; the finalized `marked` array is neither reassigned nor passed by address in that loop.

The minimal change is to compute `has_references := AnyReference(marked)` once immediately after `ScalarWritten`, pass it to `ScalarEmits`, and use it for both decisions, retaining the second decision's `|| length(retained) > 0`. No global or persistent cache is needed. This removes up to two full parameter scans per function: with F functions and P non-reference parameters, up to 2FP visits become P. At F=P=1,024, that component falls from 2,097,152 visits to 1,024. Reference-first inputs short-circuit, and earlier rejection can skip the second consumer, so this is a bound rather than a measured end-to-end speedup.

A focused pure-transform comparison should cover empty/non-reference/reference-first/reference-last/mixed inputs, references belonging to other callables, retained values with no references, write-fact changes without reference changes, and separate invocations after flags or array length change. Count visits alongside semantic comparisons. The real compiler integration still needs the accepted-builder gates above. Changing input source does not accelerate the immutable c5 executable currently compiling it; this improvement primarily targets execution of a subsequently built compiler. No compiler source change was made in this diagnosis.
