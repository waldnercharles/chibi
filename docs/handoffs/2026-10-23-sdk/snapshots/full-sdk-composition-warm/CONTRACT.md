# Full SDK Project composition draft

Status: source-only, uncompiled and unexecuted. This is a mixed-baseline patch, not an eligible integrated tree. Preserve frozen ProjectTools tree47bbe00d and typed tree6ef6b76e5c7e83b328e2973cc15c42fc79804043. No source import, build, export, test, provisioning, publication or default change is authorized by this draft.

## Ownership and API

The existing tools/package_sdk.py owns the complete temporary SDK stage, publication, prior-generation recheck and rollback. CMake owns installed native products/archives/headers and their provenance. SDK.chibi owns Project policy preparation and readiness. Compiler/default owner owns public launcher and strict frame caller. The optional flag --project-policy-source ABS enables this seam only for compiler-enabled native Darwin/Linux full SDKs with both private hosts and native bin/chibi-compile. Ordinary packaging retains its existing route.

Python stages SDK.chibi from the same repository source into its absent share/chibi/tools/toolchain/SDK.chibi path, then invokes the actual staged build host with that staged SDK source, explicit staged compiler/core roots and one of:

PrepareInstalledProject -- --prefix STAGE --build-host STAGE/libexec/chibi/chibi-build-host --compiler-host STAGE/libexec/chibi/chibi-compiler-host --assembly STAGE/bin/chibi-compile --compiler-backend sljit|bytecode --policy-source ABS

VerifyInstalledProject -- --prefix STAGE --build-host STAGE/libexec/chibi/chibi-build-host --compiler-host STAGE/libexec/chibi/chibi-compiler-host --assembly STAGE/bin/chibi-compile --compiler-backend sljit|bytecode

Arguments are exact paired unique keys. Prefix is canonical; supplied executable role paths must equal their canonical fixed installed paths and be regular executable files. Backend derives from actual CMake CHIBI_ENABLE_SJLIT configuration. Native assembly is validated against the full packager's compiled executable evidence before invocation; both private hosts retain compiled provenance validation. Authored preparation copies only the policy source, never hosts/resources. No Graph records, second prefix overlay, stage reservation/publication/removal, or public route selector is used.

The cold cache is absent STAGE.project-cache, a sibling under Python's fresh owned temporary parent, outside STAGE and all owned inputs. Selfhost Inspect must leave it absent. Final Project Inspect uses the actual Project.chibip through --artifact, with no source/core/compiler bypass flags. Native ready/status0 and selfhost cold needs-recovery/status1 are distinct checks. Invalid SDK API status2 is separate from preparation failure1 and actual Project dispatch status2.

Compiler-tools ownership rows cover these nonoverlapping destinations:

- file libexec/chibi/chibi-build-host
- file libexec/chibi/chibi-compiler-host
- file bin/chibi-compile
- directory share/chibi/compiler, including bootstrap
- directory share/chibi/core
- file share/chibi/tools/toolchain/SDK.chibi
- file share/chibi/tools/toolchain/Project.chibi
- file share/chibi/tools/toolchain/Project.chibip
- file share/chibi/project-host.tsv
- file share/chibi/project-prepare.tsv

Directory walks reject links and nonregular members. No nested bootstrap ownership row is added. Every actual regular member of owned directories is included, including tracked and ignored resources unless expressly excluded from the semantic payload below. Unrelated full-SDK public bin/include/lib/docs/engine/Build/game asset files may exist outside this ownership; package.json owns their complete file evidence. Public extras cannot alter this policy identity. Adding/changing a file inside an owned tree invalidates preparation.

Four policy outputs remain exactly Project.chibi, Project.chibip, project-host.tsv and project-prepare.tsv at their agreed fixed share/chibi paths. compiler-tools.tsv is the separate final self-excluded inventory. Input digest excludes exactly the four policy outputs and includes row order/LF. Receipt stays16scalars; witness stays10ordered metadata rows+5actual passed checks. Producer order is fixed Entry order, sorted walk per directory, then generated outputs. No whole-stage TSV and no package.json row: including the full package identity would form a hash cycle.

## Lifecycle and mutation checks

1. Existing CMake install, public docs and engine manifest finish in Python's fresh private stage.
2. Snapshot all preexisting stage file bytes and sizes; validate installed private hosts and assembly against compiled evidence. SDK source and all Project/TSV paths must be absent. Stage SDK source explicitly.
3. Authored preparation validates actual semantic compiler/core trees and staged roles; copies policy source; checks actual installed compiler bootstrap recovery and SDK.Verify capabilities939/940; rejects any owned input changes.
4. Export Project once from actual staged compiler/resources. Both entry selection checks use that same actual prepared artifact. The copied host/no-source layout and prepared SDK.Verify exercise actual prepared load and capabilities; protocol numbers alone cannot mint a passed witness.
5. Write witness+receipt only after actual proof and stable source/artifact/role/input hashes. Write owned-only TSV last. Check actual native-ready and selfhost-cold Inspect, exact frames and complete immutable receipts/inventory.
6. Python rejects every changed/removed preexisting file and every new file outside SDK source+four Project outputs+TSV. Separate authored Verify repeats receipt/witness/inventory/actual artifact readiness, with no mutation; Python compares the complete file map again.
7. Existing product/private-host/archive/build-input provenance checks run on final payload. Full package.json and build_id are generated only now, covering Project files and metadata. Recheck every non-package.json stage byte before existing prior-output recheck and atomic publication/rollback.

Any failure aborts before publication. Python removes only its owned temporary tree; authored cleanup handles only its successful proof reservations. Existing published SDKs are untouched until normal validated publication. A hard failure may retain scratch under existing recovery rules; no failed preparation becomes a package.

## Semantic compiler payload

One new tools/compiler/NativeBuildFiles.txt declares exactly four source-relative build-only files:

asset_compiler/bootstrap/native64/asset_compiler_blob.inc
asset_compiler/bootstrap/native64/asset_compiler_packaged.c
asset_compiler/bootstrap/wasm32/asset_compiler_blob.inc
asset_compiler/bootstrap/wasm32/asset_compiler_packaged.c

CMake configured copies and per-file installation share the recursively enumerated payload minus this list. Graph's compiler resource copy consumes the same exclusions. SDK standalone compiler-tools freshness compares source semantic file membership+bytes with the actual staged compiler payload; it no longer compares unfiltered raw source to filtered output. Existing warm build caches may retain exactly those four native-only leaves, unchanged and inert. Future CMake/Graph copies skip them before inspecting source or destination leaves. CMake validates selected-file destination parents/leaves before copying; Graph rejects linked/non-directory selected destination directories while walking. Nonexcluded parent links remain errors even when an excluded leaf is below them. No cleanup deletion or blanket fresh-build requirement remains.

SDK admission filters both raw source and warm build-cache membership using the source-owned exclusion metadata, comparing all selected regular file names and hashes. Any unexpected nonexcluded file/link or missing/changed selected source is rejected. The SDK installer then enumerates and copies only selected regular files into its fresh compiler stage; it never CopyTrees an admitted whole cache directory. Fresh stage validation uses the same expected semantic inventory with NO exclusions on the actual stage, so any native-only leaf there is an error. Installed Project preparation separately rejects all four excluded paths. Full packaging always installs into its own fresh stage.

At exact typed6ef source, current66 regular files remain: all48 authored Chibi modules, bootstrap/bootstrap.json and compiler.chibip, both targets' six canonical asset resources, CompilerSources.txt, runtime.json, README and .gitignore. The new exclusion metadata adds one regular file. This is not a19-input or37-input closure shortcut. Existing compiler_bootstrap package completeness reads actual recorded Chibi siblings. Owned runtime inventory includes all retained files. The separate selected-target share/chibi/assets/compiler six-file resource bundle remains unchanged.

Raw tracked C facades, ignored source blob includes, native sdk/asset_documents.cmake and graph asset_compiler/Stage.chibi staging remain native inputs/provenance. They still validate manifest resource/artifact hashes, compile source facades and regenerate their own build blob from the prepared artifact. Runtime asset_compiler_host uses embedded compiled bytes and does not reopen generated C resources. No source deletion or resource regeneration occurs.

## Review and future acceptance

Independent read-only source audits verified current CMake/Graph/SDK copy owners, typed6ef identities and native consumer separation. Candidate code still needs independent interaction review. Source-only Python parsing/diff checks are allowed; no execution proof is implied.

Future eligible combined compiler+typed+corrected Project candidate must demonstrate actual installed bootstrap recovery, export/entry retention, prepared artifact loading, SDK capability proofs, final Inspect, byte-preservation rejection, owned-file corruption rejection, unrelated public-file allowance, filtered installed payload absence, successful ordinary warm-cache retention and rejection of nonexcluded stale/aliased payloads, relocated read-only identity, native asset-source consumer correctness and normal full SDK packaging/rollback.

Known adoption hold: frozen Project f798 has a newly reported binary/resources.json pair-publication gap. No compile acceptance or prepared policy artifact freeze is claimed. Its owner must supply the corrected reviewed source/hash and output contract before execution/adoption. Protocol9 compiled-bootstrap compatibility does not establish prepared-artifact flags or these runtime proofs.

## Successor of reviewed64e32 draft

The predecessor full-sdk-composition directory and manifest9525b611 are preserved unchanged as evidence. Root held its fresh-build-only behavior because legitimate warm compiler caches contain old native copies. This successor changes only the cache/stage selection boundary and adds relevant C fixture source coverage; it does not clear execution or Project pair-publication holds.

Coverage must demonstrate ordinary warm cache copies stay byte-identical and absent from the fresh installed semantic payload; exact excluded leaf aliases are never followed or touched; malicious nonexcluded parent aliases and unrelated/missing/changed authored files fail with source bytes preserved. Existing full-tree fixture comparison is replaced by an independent semantic membership/hash oracle, not a weakened count.

Mixed-base composition also preserves typed6ef no-ckdl packaging migration: remove the obsolete SDK notice Entry and three C fixture directory/input/license references inherited from frozen47bbe. Preserve typed acb83337 semantic command oracle and all frozen47bbe opt-in cases. These small composition deltas are explicit in the full patch and per-file receipt.
