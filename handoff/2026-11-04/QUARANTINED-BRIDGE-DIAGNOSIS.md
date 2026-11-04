# Retained 7482 artifact: compatibility evidence, not seed admission

## Result

**Superseded availability blocker:** the user confirmed the old package path does not exist and requested a new build. That build now passes a raw fixed point and all 485 compiler tests; see [14F7-REBUILD.md](14F7-REBUILD.md). The observations below remain the record of the earlier retrieval/compatibility investigation.

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

The newer retained PR-head 6b46 and 72598 candidate logs likewise show matching raw stage3/stage4 hashes followed by a runner parity failure. They were not downloaded or admitted in this pass. The latest 100 releases contain no 14f7 native seed; the latest native seed is still c5. An exact-head workflow query for 14f7 returned no runs. The previously reviewed 14f7 package is recorded in the original handoff but its payloads have not been found in this cloud workspace. Its provenance records the exact retrieved 7482 SHA as the Linux builder used for the accepted 14f7 output.

## Containment and limits

The inspected ELF is x86-64 Linux, with the standard system interpreter and `libc.so.6` dependency, and no RPATH/RUNPATH. Inspection used `readelf`, not `ldd`. Exact 7482 source confirms that `--emit-llvm` writes the module bundle and returns before LLVM/linker invocation. Each probe used jobs 2, a 14-second deadline and the existing owned cleanup controller. There were at most three cases, with a 45-second aggregate admission window. Isolated output/cache locations were used; no installed seed, pin, release, product or generated executable was changed or run. The candidate was returned to non-executable mode after completion.

The existing full bootstrap was neither restarted nor duplicated. No later whole-compiler stage was launched, and its unresolved receipt remains a separate blocker. No software installation or saved/user-computer environment was used.

## Admission work still required

1. Preserve the exact producer's failed core and mandatory runner contracts. Independent exact-source tests can establish separate compiler evidence, but cannot make that historical CI run successful or waive required integration checks. The whole-successful-producer rule in the handoff specifically gates Windows consumption.
2. Normal acceptance still needs exact-source compiler behavior, complete required tests and raw fixed-point evidence for the target. Tiny emit-only probes do not establish these.
3. Published-c5 admission and supported-platform requirements remain. This Linux diagnostic does not authorize Windows consumption of a partial producer or change any platform gate.
4. Retrieve the already reviewed exact 14f7 package if an accessible copy is located, or reconcile the original bootstrap owner and terminal outcome before scheduling any replacement bridge build. Read-only artifact retrieval is already permitted; it does not require a separate approval. Do not increase the 90-minute limit as a diagnosis.

## Exact 14f7 payload availability

The original handoff records the reviewed package on the prior local machine. The intended action is to copy its existing Linux payload and proof archive into an isolated directory in this cloud workspace, then check the recorded immutable hashes. This is retrieval, not a request to rebuild or publish it.

- Expected `chibi-native-linux-x64`: 10,700,816 bytes; SHA-256 `1dd9bf1518645f50f06d4ff1912159945b967e6ad9c7b1f3228fbb32e5d68ab9`.
- Expected `chibi-proof.tar.gz`: SHA-256 `bbe71fabe3c6618d4e4b57a1c3ce9c7b519271f0ba236763251b8213aad10114`.
- On November 4, the exact proposed release and tag endpoints both returned 404, and the exact-head Actions query returned no runs. The available cloud handoff has eight metadata/source-inventory files, neither payload. Workspace searches found no binary of the recorded size; connected file searches found no matching payload title. These observations do not prove that no copy exists elsewhere.
- Ordinary downloading from an accessible artifact or user attachment is allowed. The unavailable copy is not a confirmation-policy block. Accessing the prior local machine would conflict with the current no-user-computer instruction, so that route was not attempted. An existing accessible link or attached package would enable direct verification without a new download approval.

## Small source improvement ready for a focused change

Current `package_compiler/Scalar.chibi` calls `AnyReference(params)` twice inside `ScalarEmits`, at lines 4362 and 4433. `ScalarEmits` has one call site, inside the per-function loop at line 4867. The predicate reads only `Param.reference`; the finalized `marked` array is neither reassigned nor passed by address in that loop.

The minimal change is to compute `has_references := AnyReference(marked)` once immediately after `ScalarWritten`, pass it to `ScalarEmits`, and use it for both decisions, retaining the second decision's `|| length(retained) > 0`. No global or persistent cache is needed. This removes up to two full parameter scans per function: with F functions and P non-reference parameters, up to 2FP visits become P. At F=P=1,024, that component falls from 2,097,152 visits to 1,024. Reference-first inputs short-circuit, and earlier rejection can skip the second consumer, so this is a bound rather than a measured end-to-end speedup.

A focused pure-transform comparison should cover empty/non-reference/reference-first/reference-last/mixed inputs, references belonging to other callables, retained values with no references, write-fact changes without reference changes, and separate invocations after flags or array length change. Count visits alongside semantic comparisons. The real compiler integration still needs the accepted-builder gates above. Changing input source does not accelerate the immutable c5 executable currently compiling it; this improvement primarily targets execution of a subsequently built compiler. No compiler source change was made in this diagnosis.
