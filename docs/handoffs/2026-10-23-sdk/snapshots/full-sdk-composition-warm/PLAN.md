Summary: Prepare a source-only full SDK composition patch in ignored scratch. Keep SDK47bbe and active typed6ef unchanged. Existing Python package orchestration owns temporary stage/publication; authored SDK policy owns installed Project preparation and readiness.

Context: ProjectTools installs a separate graph compiler-tools prefix. The full SDK packager currently installs CMake products and computes package.json independently. Whole-stage compiler-tools TSV would include unowned public files and create a package.json identity cycle.

System impact: Define explicit compiler-tools ownership in a full stage, bind actual staged hosts and bin/chibi-compile, and finalize package identity only after authored preparation/readiness. Keep public launcher integration with compiler owner.

Approach: Add PrepareInstalledProject and VerifyInstalledProject authored entries over an actual installed prefix; add explicit opt-in package flag. Python installs SDK.chibi, invokes those entries and verifies final package bytes before existing publication. No Graph record assertions or distribution overlay.

Changes:
- tools/toolchain/SDK.chibi: narrow installed-stage entries, owned-only TSV, shared actual capability proofs, fixed-path role validation and immutable-input checks.
- tools/package_sdk.py: opt-in orchestration before package identity, unchanged ordinary route, complete byte recheck before publication.

Verification: Source-only independent review now, including actual CMake resource/wrapper audit. Future eligible combined candidate must execute installed preparation/readiness, relocation identity, ownership corruption controls and ordinary packager tests. No builds/tests/exports while CI holds execution.

Scope update: exclude generated asset_compiler_packaged.c and asset_compiler_blob.inc from configured/installed semantic compiler resources, preserving their source-tree native build consumers and provenance. Audit full runtime resource classes and manifests before choosing payload. Add sdk/CMakeLists.txt source delta; no active source edit or execution.

Review correction: avoid all warm native-copy deletion. Reject stale excluded entries and aliased destination parents, requiring a fresh semantic build payload. This resolves the independently identified source-unlink risk without adding a new filesystem-type capability.

Root correction: fresh-build-only behavior rejected. Leave exactly4legacycachednativeleaves inert, filterbothsource/cache duringSDKadmission, copyselectedregularfilesintofreshstage and verifystrictstagewithoutactualexclusions. Addsemanticfixturecoverage; preservepreviousreview64e32 evidence.
