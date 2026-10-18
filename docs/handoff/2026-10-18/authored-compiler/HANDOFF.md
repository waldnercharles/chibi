> Historical technical record, with coordination-only wording removed. Read the top-level README.md for current dependency heads, publication status, omitted reference copies and the parser admission update. Historical source-only status and earlier hashes describe preparation, not a passing build or runtime result.

# Authored source-to-artifact compiler checkpoint

Status: local source-only candidate, unbuilt, unexecuted and unadmitted. No
remote mutation, branch, commit, PR, product configuration/build/test or workflow
dispatch occurred. This packet is ready for independent source review, not for
landing or execution.

## Implemented outcome

The authored `bounded_compiler.Compile` entry accepts an entire explicit bounded
package and returns an owned executable closure or an authored diagnostic.
It calls source_snapshot.Build, uses analysis_state's package/document mapping
and retained signature_reader tree, checks every declaration/body, reuses the
existing signature/lexical identity components, constructs typed scalar rows,
and calls scalar_lowering.Lower. A narrow private producer capability finalizes
and encodes its inner module; Artifact.chibi constructs the complete CHIBICLO
envelope itself. This is implemented code, not an unsupported-only harness or a
wrapper around native source analysis.

Supported input is exactly one import-free package across one to three files,
with one nongeneric explicitly pure zero-argument int/bool function and one
scalar return. The source subset, semantic restrictions and byte/row bounds are
in candidate/docs/bounded-authored-compiler.md. Unsupported whole-package
contents reject before payload encoding. The native callback receives no source
text, parser nodes, semantic analyzer or package-preparation request.

The exact current target is c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb. Genuine
dependencies are PR445 1338151c084c1a633f881153084bc83773df27fa, then PR446
70ff5bf54a1125a110be6a26a7e226a4b5d542ba, plus PR447
bdbb896c48aadd271ee63a6562447ccb95d7ffa1. Their runtime validation remains
independent required work. Neither held PR407 nor PR420 is imported.

## Artifacts

- candidate/: twelve complete new repository files, including three authored
  source files, four native producer/test bridge files, four C test/build files
  and one explicit contract document.
- authored-compiler-driver.patch: those new files plus additive CMake/dispatcher/
  policy registration and PLAN checkpoint hunks. SHA256
  1b6db1fa96f49358d4bf05c4e8ed8d3533900142a1782f7c7d460c0dc9e19924.
- new-files.patch: the twelve independent new files. SHA256
  d828873a3e1a61e9dfad4083467c3999a0914eb4cf0f1b723ccdc97bd3c08a0f.
- manifest.json: authoritative current new-file hashes, pinned dependency blobs,
  complete source-byte counts and finite workload. Later source fixes supersede
  the earlier test packet's 179,213-byte source accounting.
- dependencies/: all fourteen PR446 reader-closure sources, freshly fetched at
  its exact head and independently matched to their Git blob SHA. These total
  exactly 129,840 bytes. The exact PR447 Lower source is in reference/ and is
  22,435 bytes with blob 00d8071592b34c3e1b5765efdf470d9dbd9643a7.
- native-bridge/HANDOFF.md and driver-tests/HANDOFF.md: boundary, ABI, lifetime
  and workload details. Root manifest is the latest source identity authority.

The full new compiler closure is eighteen files/sixteen packages, 179,267 bytes,
including the separately owned corrected Lower.chibi and both union separators.
Its new fixture has an independent exclusive 256-KiB total and 32-KiB per-source
cap. PR446's old exclusive 128-KiB fixture cap is unchanged.

## Integration warning

Do not replace existing files with old complete projections. None are included
in candidate/. Registration-only hunks originate at the exact PR447 reference;
the CMake context before scalar_lowering's target_sources differs from PR445's
extra native_prepared link. Mechanically reanchor that single include after
integrating real dependencies. Preserve PR446 analysis_state embedding and
snapshot-reader selector, PR447 scalar registration and current PR444 semantic
additions. The PLAN hunk inserts an isolated source-only section before M6.
No combined exact target tree has been claimed or built here.

## Workload and current checks

One fixed C/pico_unit case proposes 39 requests per compiler backend, 78 authored
Compile calls, 86 offered files, 36 bootstrap parses and 70 input raw parses,
28 payload callbacks, seven direct bridge rejection checks, and 56 generated
artifact loads/invocations across bytecode/SLJIT. No user source is passed to
native semantic analysis even as a test oracle. The expected values, existing
identity algorithm and host reader are independent output checks. All source,
producer and compiler owners retire before generated artifacts are loaded.
Consumers are fresh host API instances in the same test process, not separate
runtime-only executable processes.

The bridge and test sources passed clang-format 22.1.8 dry-runs. The twelve-new-file patch
passes git apply --check --whitespace=error in an empty temporary directory;
registration hunks were separately checked against their exact historical
reference. The fourteen materialized dependency sources match their remote Git
blob IDs. These are source-materialization/format checks, not product tests.

Static review removed reserved `state` alias and `entry` local/payload names
from the driver, and added the identifier byte cap before native encoding.
After the exact PR447 syntax probe confirmed its missing union separator and
reserved Frame.branch declaration, the same missing separator was found in
Result and ScalarExpression here. Both now include the required `|`; these are
the only new authored-source changes in this refresh. ScalarSignature and
Decimal already had separators. A repeated static declaration/initializer-name
inspection found no global-reserved collision in the new three-file package.
This is not a parser-pass or semantic validation claim.

The separate scalar-lowering dependency includes the minimal Lower.chibi correction: required Plan separator and
Frame.branch -> branch_pc declaration/four uses. The corrected source is 22,452
bytes, SHA256 1778dd60224e4edb46e87d4563e20e0625b170da933976c317f00340eecfd4fd.
It is a required dependency correction, not imported into this driver's patch.
The root manifest preserves exact original dependency identities and explicitly
records the corrected source overlay used for the 18-file syntax precheck.

The superseded pre-union-fix packet is excluded from this compact handoff. Its full patch SHA256 was
db085e814916ed06896b65c15b42c46c81b53e4fdc33df84fc0b31bc2142569c;
new-file patch ace1e4de35a851810c572b17c039e2df5a702861d660ccb9a1ae3fd722078538;
manifest d0d0cfc1b25920097698ae39ecc316cf6bd94066f44a496e898c31313623cf3e.
Those hashes are superseded, not current runnable candidates.

syntax-precheck/ prepares one separate C/pico dispatcher's fixed eighteen cases,
one complete parse per compiler-closure source and one fresh parser/tree alive
at a time. It reuses the reviewed diagnostic error walk and pinned parser/runtime,
with the existing exclusive 32-KiB source and 50,000-node traversal guards and
one shared production 60-second supervisor. No authored/VM/JIT code executes.
The corpus, generated header, C dispatcher, build/run inputs and commands are
pinned for independent admission. Frozen diagnostic SHA256
a2d920d9d4f3745f2071499aa038216d1ab9f6b45694fee04f94948399957149.
No build or invocation has occurred.

## Next required action and limits

Finish exact transport/reader/scalar dependency validation, including the corrected scalar
source gate and complete new compiler-closure syntax precheck. Then independently review the combined new compiler
contract, lifetime paths and finite workload; construct one exact integration
candidate; separately admit/build it; and run the complete new selector once
under the unchanged shared 60-second production supervisor. Do not split a
timeout, infer admission from registration, or bypass current holds/protections.

The runtime-only compiler-host product remains unchanged. This compiler closure
currently needs the private producer fixture; its output artifact uses the
existing host API. Native verification/optimization/finalization, module
publication and inner prepared serialization remain explicit transitional
dependencies. There is no standalone installed producer, complete language
compiler, stage-one/stage-two bootstrap, self-hosting or final installed
build-without-CMake/Clang/GCC result. No recovery path was removed.

This handoff retains source and provenance only; no generated product
binaries, project copies or runtime artifacts are included.
