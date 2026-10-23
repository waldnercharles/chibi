# Prepared pair witness dependency proposal

Status: source-only coordination proposal, awaiting compiler/Project owner agreement. No SDK/parser code edits or native implementation imported, no build/export/test run. Preserve warm full patch7936ed238acb008383b710f6e08a66b515598fdcb9f1618a16af3bc79ea95a8a and manifest a898671b9e0b379d1e4ce3c5f65df0b78089e434e0daaf3650e53ddb1b639765. Frozen Projectf798 remains historical and is not an eligible artifact source.

## Dependency

Corrected Project pair publication must use actual prepared build.publish_pair(source0,destination0,source1,destination1)->int. Status0 commits/clean;1 notcommitted/prior preserved or restored;2 notcommitted/incomplete restoration with old backups retained;3 committed/housekeeping retained. All paths explicit absolute, same existing physical parent, regular nonlink inputs and missing/regular nonlink destinations. The shared native state machine and strict Darwin/Linux adapter are implemented by the Project/native owner in separate ignored scratch. Protocol9 and directory bindings939/940 cannot stand in for pair transport. Binding identity/source freeze remains to be supplied by that owner.

## Strict producer/consumer format proposal

Keep CHIBI-PROJECT-PREPARE/1 with its16 scalar fields, ownership/output syntax and exactlyfour policy outputs. Bump only host witness to CHIBI-PROJECT-HOST/2. Same10 metadata in existing order, followed by exactlysix check rows in order: prepared-load, prepared-without-source, prepared-entry-selection, reserve-directory, publish-directory, publish-pair; each value exactlypassed withLF. No duplicate/extra/missing rows, noHOST/1fallback, no row accepted solely from numeric protocol/catalog/source review. Project producer/consumer, SDK producer and Cfixture parser must move together against corrected Project source hash. Whole witness remains bound by final TSV/receipt identity with no new SDK output or hash cycle.

## Actual prepared proof proposal

Retain existing same-Project-artifact Inspect/Compile dispatch and prepared SDK.Verify939/940 tests. A separate generated pair-probe source closure avoids placing the new direct native call in legacy SDK.Verify/CompilerTools module effects. Export it with the actual staged prepared-capable host/compiler/core; run its actual prepared artifact on the byte-identical copied host in the existing isolated prefix/payload/work layout without source/compiler/core and with emptyPATH/falseCC/CXX. Bind/recheck actual staged/copied host and Project artifact hashes throughout; no source-mode invocation counts as prepared execution.

In private owned work, exercise all four prior destination presence combinations. Supply two regular staged files and two destinations under the same physical parent. Require actual publish_pair result0, exact new bytes on both destinations, both staged source names consumed and no leftover transaction entries. Clean only the private probe fixtures. Normal status3 is not an acceptable clean SDK capability probe even though Project application semantics treat it as committed. Emit publish-pair passed only after all actual prepared calls and postconditions succeed, then final Inspect/inventory/stable-byte checks as before.

This capability witness does not claim deterministic failure/rollback/cleanup outcome2/3 validation. Those require actual native/shared fault-injection Ccoverage and real Project integration. Intentional rejection can produce transport diagnostics; do not invent an exact stderr contract before the owner supplies it. No prepared artifact is frozen until the corrected Project source, native binding metadata and strict witness consumer are agreed and eligible.

## Successor boundaries

Any SDK probe and strict Cparser source changes will be a separate reviewed successor to the preserved warm candidate, with exact per-file/full/minimal-delta identities. Compiler/Project owner owns native implementation, binding contract, corrected Project policy and its strict witness expectation. SDK owner owns actual prepared proof/producer and Cfixture parser. Root coordinator admits combined execution and final adoption; all current execution holds remain.
