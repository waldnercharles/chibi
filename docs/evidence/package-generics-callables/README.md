# Exported generics and callable inputs source checkpoint

This is a source-only draft for review. It exports generic package functions and
admits named functions and capture-free inline functions as synchronous,
non-escaping callable inputs. It does not admit stored or returned callable
values, captures, unknown host forwarding, or callback signatures with multiple
results.

The code is based on `3e2b28dc19fb21961ae06fe99aeb5a8dede35bf0`. Its reviewed
51-file feature patch is
`5c5eb656994de4e6b40a49092d480d69ca6c985af0b5820f1f162da974625887`.
The protocol-13 repair at that base was incorporated without changing the
feature patch. Compiler and language execution evidence below belongs to the
frozen `7c6ca2f5d2c0fd10b4577f2c319ab5680b66ad62` snapshot plus that patch and
the enumerated test-only fixture changes. It is not compatibility evidence for
later accepted runtime changes, including the arena layout change in #797.

## Review scope

Independent native, grammar, and authored compiler reviews covered retained
definition environments, generation teardown, view types, persistent mutable
type tables, nested collection and optional inference, canonical specialization
identities, erased and labeled arguments, and ownership at direct callback
calls. The final fixes were independently reviewed again; no source findings
remain open.

Regressions cover scoped and managed pointer callbacks, parameter shadowing,
source evaluation order, copy isolation, repeated specialization, capture and
escape rejection, and a positioned rejection when an inline function is passed
to a scalar parameter without any templates.

## Recorded validation

| Check | Result |
| --- | --- |
| Import conformance using the bytecode compiler | 2/2, 17,552 assertions |
| Import conformance using the compiled compiler | 2/2, 16,540 assertions |
| Scalar facts and LLVM IR | 4/4, 49,151 assertions |
| Compiler argument sharing | 1/1, 181 assertions |
| Exact source inventory and packaging bounds | 4/4, 24,890 assertions |
| Compiler self-bootstrap fixed point | 1/1, 32,682 assertions; 613.906 seconds command wall time |
| ASan/UBSan native imports, generic/query and syntax checks | 151/151, 16,137 assertions |
| Local artifact writer and recovery | Passed; 104.138 and 123.584 seconds |

The import fixtures include 20 added runtime calls and an invalid-inline
argument rejection on both compiler routes. The sanitizer driver and linked
project C code were instrumented; vendor archives and generated JIT code are
outside that instrumentation claim. The snapshot used an asset-test option
that defaults ON, switched OFF only for seed construction, plus the documented
fixture initializer repairs and one fixture-target warning demotion. These are
not changes in the feature patch.

The experimental compiler closure is 21,842,899 bytes, SHA-256
`5e06b70409a10a047172c804446ef68e2f687e6fd1f7ba34d284b5639eab9f6d`.
Its metadata honestly records `7c6ca2f5d2c0fd10b4577f2c319ab5680b66ad62-dirty`,
version 8, minimum 6, and all 28 matching compiler sources. It is not checked in
or approved as a shared compiler seed. The checked bootstrap files are unchanged.

[validation.json](validation.json) records source identities, exact counts,
the fixture boundary, prior-variant coverage, and raw and saved log hashes.
Saved logs abbreviate local checkout paths and remove trailing whitespace;
their raw hashes are retained.

## Integration and publication gates

The package-test suite remains 5/6 because plain-array algorithms fail parallel
admission. The same conflict reproduces on accepted `7c6` with an ordinary
array and no generic or inline functions: both VMs report source purity and no
source effects, execute successfully, but fail the separate host-purity check.
The intended parallel assertion and an actual view-read serial control remain
in place. [Baseline evidence](baseline-admission.json) records the unchanged
production hashes. The coordinator assigned the declared-effect repair to
#792/#801. `language_values` is a representation contract and does not justify
a host-purity exemption; this draft changes no runtime policy.

The temporary asset-enabled snapshot build timed out while compiling the
facade at the existing 240-second limit. Its total build time was 282.903
seconds. [Failure evidence](asset-failure.json) retains the request, compiler,
host hashes and phase. Shared-host producer load was not isolated, so this is
not a feature-only performance attribution. The limit was not raised.

Before landing, reconcile the then-current accepted fixture, EmitBody,
reachability, library and foundation prerequisites in the coordinator's order.
Rebuild matching native objects and headers after runtime layout changes;
never mix the old arena ABI with newer objects. Resolve capability/effect
admission, regenerate one matching clean-source compiler artifact in the
assigned slot, and validate the ordinary asset build and installed SDK.
This draft does not clear compiler artifact, SDK, release or merge holds.

Full native/material/project/language/tools suites, the platform matrix, GPU
execution, and final combined-source SDK/Graph acceptance were not run for this
checkpoint. New broad local producers require the coordinator's resource slot.
No test-duration ceiling or timing-based admission rule is introduced.
