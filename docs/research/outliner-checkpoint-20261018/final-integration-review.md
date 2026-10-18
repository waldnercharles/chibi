# Combined engine integration review

Reviewer: `/root/outliner_m3_integration`. Read-only source, index, ancestry,
installed-header, compile-record and retained-log inspection. The only output
written in this review is this report. No build, test, authored preparation,
fetch, publication, source edit, index update or commit was performed.

## Conclusion and boundary

No new blocking defect was found in the reviewed request-work implementation,
its VM/SLJIT/Wasm/generated-C boundaries, prepared ABI, scalar-append interaction,
or the M3 merge resolutions. This is source-review acceptance of the hashes below,
not final validation or landing approval. The candidate is still a dirty merge:
`HEAD=ff6d06c888fe660894776255ec7f9fc507db6393`, sole
`MERGE_HEAD=26e0821419e753f42041383bbe4f25db77b62121`, merge base
`6ea2221c97d266c9bb028443cb67bd445fdb3476`. There are no unresolved index entries;
both staged and unstaged `git diff --check` are clean.

The incoming side consists of exactly seven commits, from `9ea13c24` through
`26e08214`, ending at published M3 explicit package roots and portable core
strings. The locally known later head `f32849e14d524e1a0108b8bfccd33f9c6e4f8ca3`
(`Port shared unit algebra to Chibi`) is not an ancestor of either merge parent.
Thus the pending merge is pinned M3, not the later M4 branch. This ancestry check
is not a claim that arbitrary unstaged content can be classified by ancestry;
the separate Outliner/scalar/fuel changes are reviewed as local integration work.
No remote state was fetched or other worker branch changed. Existing holds and
other-worker landing ownership remain in force.

## Runtime and ABI findings

All fourteen production fuel files match every entry of the independently
reviewed `request-fuel-draft/ENGINE.sha256` byte for byte. That manifest itself is
`95af69dc75b23505bae0ebec0d31c3860f47aa8e1c6a8f269c32bf5be7b3a8b1`.
The combined files already contain the preceding authority and scalar changes;
this comparison is not against an older pre-integration runtime.

- The token owns its finite account. Queued events and activations retain pending
  references; host requests retain a plain reference. A completed request can keep
  its activation pending until the host releases its reference. Cancellation,
  nested dispatch and resumed/task/continuation execution cannot mint grant
  authority. Proof lookup walks runtime-owned contexts instead of dereferencing
  fabricated parent or inspection pointers. The one absolute grant preserves
  spent work, rejects reductions and repeated/foreign grants, and never changes
  the cached legacy countdown base.
- Synchronous frames inherit the same account at creation and refresh. Cached
  frames clear borrowed pointers after callback/host reset and before the origin
  can be freed. Activation retirement and late detached requests retain existing
  lifetime rules. An ordinary dispatch still uses its original per-frame budget.
- VM and generated C short-circuit the legacy check before charging work. SLJIT
  first checks available work but debits only after the legacy check succeeds;
  its separate work-exhaustion edge avoids the countdown failure edge's +1 undo.
  The float-only scratch layout preserves variables/steps/context registers.
  Actual Wasm uses its existing imported machine-step checkpoint path, not a
  bytecode fallback selected by the work policy.
- Every fast/proven-loop selector that removes checkpoints rejects opted-in work
  contexts. Scalar append and row runtime bodies remain eligible because they
  retain canonical `BF_CHECK` operations. Direct prepaid roots without a checked
  body fail before execution with the specific STATE/unsupported-policy error;
  ordinary prepaid entry behavior is preserved.
- `prepared.c` fingerprints `sizeof(execution_work)`, spent/limit offsets and the
  context work-pointer offset/size. Wire headers compare target pointer size and
  ABI before decoding. Decoded definitions are verified/prepared again; derived
  scalar append metadata is not serialized as authority. `suspend.c` rebuilds
  runtime/borrow/row/scalar plans from canonical code. No project format migration
  is required for this additive host contract and prepared-cache ABI invalidation.
- Scalar append private-tail roots are included in data tracing/visitation;
  reset/rebind/fresh-entry paths clear cached builders. Canonical get/length
  readers remain types-owned callbacks, with strict identity/shape checks. The
  new private optimization changes host-call implementations, not checkpoint
  placement or the prepared/public collection representation.

## M3 and installed ownership findings

The sibling callable resolution correction preserves authored shared-action
precedence while allowing an authored function to shadow a same-named native
action. Its fixture checks exact sibling symbol/capability and awaited/task/effect
metadata in both document orders. Package signatures use current `int` syntax;
unsupported exported templates still have a strict negative diagnostic fixture.
The catalog identity correction accepts duplicate views of the same nominal ID
while keeping different-ID ambiguity sticky. Record-publication exercises the
self-declaring library and importer together with grouped tuple return syntax.

The build host only adds `LD_LIBRARY_PATH` to the existing allowlist; bounded
output/process limits are unchanged. The Web SDK recipe explicitly chooses
Ninja; the matching recipe fixture still checks the exact generator and every
remaining definition argument. M3 source packages/core and private build-host
installation remain present.

`authority.h` and `prepared_producer.h` are owned public exports of
`Chibi::execution`; the context layout is owned by `Chibi::execution_contract`.
`work_internal.h` remains private. Scalar reader implementation is types-owned,
with execution consuming only its private copied headers through declared target
links. The SDK ownership contracts are still enforced; there is no engine-source
include shortcut for the installed Studio consumer.

During review SDK10 completed atomic packaging. Observed native package ID:
`778b78b7b14216ea6f3445b4b3ff23abe0efa609e7ee9d717b88c03c2a52fa27`;
source snapshot `8cf9fcb246917d70fbd688b891ba6880378078cba5a5668a4ff989ffeb96abdf`.
The metadata truthfully marks the source dirty at HEAD `ff6d06c8`. All 1,562
SDK compile records contain `-fsanitize=address,undefined`. Installed function,
authority and prepared-producer public headers, and installed core Strings,
match current sources byte for byte; no `work_internal.h` is publicly installed.
These narrow checks do not replace full package verification, relocation or a
consumer rebuild. Earlier unsanitized Debug SDK evidence stays separately labeled.

## Measured evidence inspected

These are retained parent-run logs, not executions by this reviewer. File names
are relative to `/workspace/outliner-build`; exact hashes are recorded below.

| Evidence | Result | Limits of conclusion |
| --- | --- | --- |
| test-request-work-3.log | Native 5 cases / 815 assertions, 0.216s | VM plus actual SLJIT; final fixture e7e08b4f |
| test-wasm-request-work-1.log | Wasm 5 / 815, 0.216s | VM plus actual generated Wasm; not browser/render evidence |
| test-request-work-c-1.log | Generated C 1 / 107, 0.313s | Two tiny definitions, one compiler child, 7 C + 7 VM calls |
| test-scalar-append-5.log | Native scalar 6 / 1717, 0.189s | Prepared round-trip and debug pause/resume combinations not covered |
| test-wasm-scalar-append-1.log | Wasm scalar 6 / 1716, 0.256s | One native-only direct-entry assertion is absent by platform |
| test-outliner-work-3.log | Final-ABI census 1 / 393, 0.475s; K=249 | 14 reachable canonical bodies; zero runtime controller calls |

The request fixture's two failed earlier runs are retained. Their corrections
release the completed Hold host reference and replace a yield-resume pump with
one update epoch; no production scheduling or fuel policy changed. Final bounds:
12 modules, 6 tiny IR definitions, 40 tokens/max4 live, max1 host request,
10 emits, 20 dispatch API calls, 12 pumps + 2 updates, 14 direct calls + 2 outlined
children, recursion3, authored loop32, limit/grant<=1000. The explicit Wasm and
C routes remain standalone; none was added to a broad aggregate.

The final ABI census qualifies K=249 only for the frozen authored/controller,
lowerer, catalog/preparation and checkpoint identities in
`census-final-abi-review.md`. It is not a native-operation, wall-time or arbitrary
edited-program completion bound. No redundant census is needed for fixture-only
lifetime/scheduling corrections; relevant source/ABI changes require requalification.

## Remaining validation and explicit gaps

- The final native/web SDKs and consumers must share the new context ABI and
  packaged source identity. Native SDK10 packaging is now observed; final web
  packaging and explicit installed Studio configure/relink remain separate gates.
  SDK identity compile definitions must match the new package for all three
  consuming targets, including acceptance. Old object timestamps are insufficient.
- Final installed/relocated public-header/archive/core-source checks, owning-target
  checks and relevant M3 package/build-host/CLI checks must use the current combined
  artifacts. Historical M3 branch results do not establish this candidate.
- Existing prepared/module checks cover the changed ABI mechanism, but no new
  request-account or scalar-I32 prepared-image round-trip was executed in the
  cited focused fixtures. The work fixture does not separately execute a grant
  at exactly the initial limit or after nested dispatch; the original authority
  suite covers nested proof revocation. Generated-C recursive helper fuel is not
  separately measured. Scalar debug pause/resume and cross-arena retained-prefix
  combinations remain explicit coverage gaps, not asserted passes.
- Final mounted Studio fuel arithmetic/live integration, source/reload guards,
  two-frame startup, and browser/rendered interaction checks remain governed by
  their own admitted workloads. The scale diagnostic's enlarged direct budget
  is not proof of production Capture grant or UI completion. Published valid
  models are preserved on later handler failure; this is not handler rollback.
- The project group requires its ordinary unsanitized Release `chibi_ctl` because
  its repeated catalog-start processes exceed the deadline under ASan. This is
  a documented execution profile, not a reason to weaken fuel assertions.
  Separate rendered-selector artifacts can be ordinary Debug with assertions
  enabled and a reviewed debug-only strip copy; the 64MiB cap must not be raised.
- Preserve shared 60s test deadlines, 10s fixture child caps, quarantines and
  explicit holds. The historical language aggregate timeout is not a pass and
  cannot be reconstructed from chunks. Missing affected-contract/required-server
  checks still block landing. No server state or landing eligibility was verified
  by this read-only review.

## Practical remaining command matrix

These commands are requirements to be performed by the integration owner, not
commands executed by this review. Set each path to the matching final artifact;
run each admitted selector under its own unchanged production supervisor. Do not
turn the focused routes into chunks of a quarantined aggregate.

| Contract | Invocation / environment |
| --- | --- |
| SDK packaging and ownership | `CHIBI_TOOLS_BUILD_DIR=<rebuilt-tools> python3 tools/test_policy.py tool package`, then separately `tool package-ownership` with the same environment |
| Linux desktop dependency relocation | `CHIBI_TOOLS_BUILD_DIR=<rebuilt-tools> python3 tools/test_policy.py tool desktop-dependencies`; this standalone route is not implied by the default tools group |
| Current M3 source packages | With `CHIBI_LANGUAGE_BUILD_DIR=<rebuilt-language>`, run each of `tool language-package-graph`, `language-package-syntax`, `language-package-execution`, `language-package-resolution`, `language-package-imports`, `language-package-files`, `language-package-core`; these remain outside the language aggregate |
| Build host / CLI | Matching rebuilt drivers for `language-build-workflow`, `build-process`, `build-toolchain`, `chibi-cli`; retain actual private installed host and loader-environment identity |
| Relocated installed language consumer | After relocation: `cmake -S test/language -B <installed-language-build> -DCHIBI_INSTALLED_SDK=ON -DCMAKE_PREFIX_PATH=<relocated-sdk>`; build separately, then select that driver with `CHIBI_LANGUAGE_BUILD_DIR` for the public package routes and `language-headless` |
| Relocated installed tooling consumer | `cmake -S test/tools -B <installed-tools-build> -DCHIBI_INSTALLED_SDK=ON -DCMAKE_PREFIX_PATH=<relocated-sdk>`; build separately, select with `CHIBI_TOOLS_BUILD_DIR` for admitted `native-tools`/ownership coverage |
| Installed prepared runtime | `tool record-installed-prepared` also needs `CHIBI_RECORD_RUNTIME_BUILD_DIR=<compiler-free-runtime-consumer>` and the correct tools driver; retain actual compiler-free installation evidence |
| Release-dependent projects | Build the applicable full engine/compiler/SDK Release profile separately; use unsanitized Release `CHIBI_CTL=<release-controller>` for the admitted project group. A tiny Release probe in package-ownership or an SDK10 Debug pass does not establish this profile |

For Release configure `-DCMAKE_BUILD_TYPE=Release` and the root's supported
`-DCHIBI_ENABLE_SANITIZERS=OFF`, or an equivalent fresh standalone SDK Release
configuration. The standalone SDK does not consume that sanitizer option: verify
actual C/CXX and linker flags and do not inherit the task's instrumentation cache
or claim sanitizer state from an unused cache variable. Final target coverage
still includes applicable native compiler/player/controller and web artifacts.

Installed package APIs are owned by `Chibi::source_catalog`; catalog snapshot
storage is owned by `Chibi::capabilities`. Configure the relocated consumer only
after relocation so its generated core header reads
`${Chibi_CORE_SOURCE_DIR}/strings/Strings.chibi`. The complete language dispatcher
intentionally contains private fixtures (including package syntax and runtime
internals); only the public-package fixture translation units can be claimed to
have installed-public-header coverage. Do not call that whole dispatcher a
source-free build. Record compile/include and link provenance accordingly.

Existing finite inventory remains: core4cases/12preparations/66budget-10000calls;
files8cases/22loads/12preparations/10budget-2000calls. Keep their already admitted
bounds and production deadline; no new tests are admitted by this review.

## Prepared SDK10 installed-consumer qualification

No existing installed language consumer can be reused from the inspected task
build roots: eleven CMake caches under `/workspace/outliner-build` were inspected
(excluding dependency, generated object, installed header/library/resource and
VCS directories); none has `CHIBI_INSTALLED_SDK=ON`. Native/release/tools-only and
both Wasm-language caches explicitly have it OFF. The proposed two new paths
below were absent at review. This plan has **not been executed**, and no copy or
configuration was created by this reviewer.

Use the already packaged SDK10 ID
`778b78b7b14216ea6f3445b4b3ff23abe0efa609e7ee9d717b88c03c2a52fa27`.
Relocate an exact owned copy before configure; do not edit its manifest, headers
or libraries. A symlink back to the original prefix would not establish this
relocation requirement. The source/include provenance limits described above
still apply to the complete dispatcher.

The existing task-local cache file
`/workspace/outliner-build/instrumented.cmake` has SHA-256
`3906c764c1a590ef2b2717ae57b9e556b3dba70edfae733e012e40c026f57d68`.
It explicitly sets C/CXX ASan+UBSan/frame-pointer compile flags and executable,
shared and module link flags; its C warning exception is the existing cloud
profile. Using it avoids relying on the unsupported standalone-SDK sanitizer
option. Keep the sanitizer runtime environment unchanged; no leak/sanitizer
suppression is proposed.

Prepared setup/configure/build commands (run only after the owner schedules this
build, with no other heavy build competing):

```sh
set -euo pipefail
source /workspace/retire-examples-436/build-env.sh
test ! -e /workspace/outliner-build/installed-sdk10
test ! -e /workspace/outliner-build/installed-language-sdk10
cp -a --reflink=auto /workspace/outliner-build/sdk-native /workspace/outliner-build/installed-sdk10
cmake -S /workspace/chibi-outliner-m3/test/language \
  -B /workspace/outliner-build/installed-language-sdk10 -G Ninja \
  -C /workspace/outliner-build/instrumented.cmake \
  -DCMAKE_C_COMPILER=/workspace/.cloud-tools/root/usr/bin/clang-19 \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCHIBI_INSTALLED_SDK=ON \
  -DCMAKE_PREFIX_PATH=/workspace/outliner-build/installed-sdk10 \
  -DChibi_DIR=/workspace/outliner-build/installed-sdk10/lib/cmake/Chibi
cmake --build /workspace/outliner-build/installed-language-sdk10 --target test_language --parallel 2
```

This compiles the existing language dispatcher and headless consumer object; it
does not rebuild the engine SDK or fetch dependencies through the SDK source
branch. Configuration/build timing stays outside the test deadline. Before
execution, retain the actual configure/build log, package ID, generated core-header
input path, compile commands and final link command. Verify every consumer
compile has ASan+UBSan plus fixture assertions (`-UNDEBUG`), actual linker flags
include sanitizer runtimes, and imported archive/header paths resolve under the
relocated prefix. In particular the public core/import fixture translation units
must not acquire `${engine}/src` include access; record-publication intentionally
has an explicit private fixture include and cannot prove that same property.

The minimal distinct installed qualification uses four existing routes, each a
separate admitted shared-60s invocation. It does not repeat all package tests or
the complete native language suite:

```sh
CHIBI_LANGUAGE_BUILD_DIR=/workspace/outliner-build/installed-language-sdk10 python3 /workspace/chibi-outliner-m3/tools/test_policy.py tool language-package-core
CHIBI_LANGUAGE_BUILD_DIR=/workspace/outliner-build/installed-language-sdk10 python3 /workspace/chibi-outliner-m3/tools/test_policy.py tool language-package-imports
CHIBI_LANGUAGE_BUILD_DIR=/workspace/outliner-build/installed-language-sdk10 python3 /workspace/chibi-outliner-m3/tools/test_policy.py tool language-record-publication
CHIBI_LANGUAGE_BUILD_DIR=/workspace/outliner-build/installed-language-sdk10 python3 /workspace/chibi-outliner-m3/tools/test_policy.py tool language-headless
```

Core exercises installed/relocated shipped Strings; imports checks owned package
exports/defaults and actual backend linking; record-publication checks the merged
same-identity declaring-library/importer route; headless executes the separate
object consumer linked only to `Chibi::behavior_runtime`. Other affected native
package routes can reuse their final native-tree evidence when unchanged; this
focused installed matrix is not a replacement for a missing distinct contract.

The root `package-ownership` route is necessary for changed packaging metadata,
but it builds a tiny synthetic ownership fixture, not the actual SDK10 consumer.
It therefore cannot replace these installed/core/import/publication checks or
actual relocated header/archive resolution. It also does not establish the full
engine Release profile. Keep it as the separate existing package-contract check.

## Exact reviewed source hashes

Paths below are relative to `/workspace/chibi-outliner-m3`. This is a content
snapshot, not a final clean commit/tree ID.

| File | SHA-256 |
| --- | --- |
| `src/execution/authority.c` | `48f92bc1267d27134bf3de6a9983e85dff33f556c15954fe6d0bc4de447331bf` |
| `src/execution/authority.h` | `852a77301c443793d03ab873fcc9b44704e88347f994b9894319b4d40de0d07b` |
| `src/execution/authority_internal.h` | `554f74c94f2b13da6701735898f40aa96cfcd0f1ce2593073411c729d3e80075` |
| `src/execution/c.c` | `9f7e256a8a405656261ef3954c0d344518eedb22bef97070b290a3d8d8958e53` |
| `src/execution/call.c` | `bf17a0a66f520487dd05cb37f60c75fad23ca30b4a849a878281908b72e60d2d` |
| `src/execution/definition.h` | `6b5989ea1776ed5f2e0b2743c9950198fc192aca37e28ed2b0ecb10263132f45` |
| `src/execution/function.h` | `fe1e9b69f923f7169f7731e78adecb7325d03b1a98dc12a60ce58b84bc50e09c` |
| `src/execution/jit.c` | `7de3ab6e36a17c5efb8eabc5ac0e7056bf6a6013d2c22b6b89eb8792e8a73c7d` |
| `src/execution/machine.c` | `24dacc38edc7d12ff36a25c0dff84acd10d8699e1b4ecfdb608eacc030536e97` |
| `src/execution/prepared.c` | `cfec73fa439511c5d7fa368ad646e1b35cd0d01ed5ed74c56c57fc85924512a8` |
| `src/execution/program_activation.c` | `f2c41073b241c9b5f86c03623abf3478c041307e9533302798b97a5ffdf13e1e` |
| `src/execution/runner.c` | `e89a3f3266e500ebfaff95221f4dc4e90703454d95119b98a0ded3af5f9d24d8` |
| `src/execution/wasm_jit.c` | `f373d2063f857fb9034acdeffa59c0360a550ec8215f6c191df654df073c8c48` |
| `src/execution/work_internal.h` | `a3e1508d62da99923c83cc0682a8779b654d2ffb63fa4035ede5349762de8371` |
| `src/execution/program.c` | `c3e03c715e2ef8cc56d784a4c5e6c2e296b051f5776cd095a6f309e93281e830` |
| `src/execution/program_commands.c` | `64d5436cfca581dd47a0c27239010ad613ae4256697d0954cbd719ae0dc00d2a` |
| `src/execution/program_internal.h` | `8b569f1522394b43623344333c258e3681dd14139c9b067499d4eff4caf69368` |
| `src/execution/program_tasks.c` | `23c89c2a8abf64f225199494c3ee172301850000c585d31f5cba737520d5dc4f` |
| `src/execution/request.c` | `ad87bab67d54e7d3c9e3561d171549025aecbb74b56c3a42a31fbe41673d8022` |
| `src/execution/row_builder.c` | `e5f379006f46b3df4d15f97e4aa5396b73dcc65926878b803a4fcdfb605eb520` |
| `src/execution/row_builder.h` | `8aa7924349da7ae358b558478dc5040f529c2b004140e8ae4142794fd12468e6` |
| `src/execution/function.c` | `332a81e200171775ebd4f3fc5e6b63f054327ec920cf69dc8bdbd957ff8592ca` |
| `src/execution/host.c` | `de23afc818023ea5eca39e63063729e743593fde9047f90542cd6959b8cf466d` |
| `src/execution/jit_host_call.c` | `05b64f78fd0075ac3cbf77fc8bddb34be129ce81abf6fbfd3308b603c15e1d41` |
| `src/execution/suspend.c` | `c828f904b636c111e66f81c463de5ed4f03720b982fe54d4b53b3053277ac836` |
| `src/execution/storage.c` | `57b25599ae4642d058d330c3d2514d3e0b666abf7d108c3eb2c73c5ccd2e90ad` |
| `src/execution/prepared_producer.c` | `30554827c0bbc71ea37983b5bd0a2885d6163e8d87db6475e073fb9c763fd541` |
| `src/execution/prepared_producer.h` | `1f084934ad334247a0c96473a58f41819c3ef6bda34d5ee821c0dbcdf830359e` |
| `src/types/array.c` | `434e56b12ab3fd24f76dbd3067cb8b4ef3648f9ce35a8153227c7c362c5894ef` |
| `src/types/array_internal.h` | `b3e0303358417e280bef2f0a8777e463f69853aab014905098649c3f03b96972` |
| `src/runtime/arrays.c` | `3ded0918dc86c5aacdd85b3fad23fdc02bc77827ee33d81e57ff0d81ed066b74` |
| `src/runtime/arrays_definition.h` | `8258abf432650976f212242c737b348c5e5924d5516a17d3eb4c47764f0be2af` |
| `src/language/lower.c` | `2658fc232b983e2d91a983998e3ad5e306963a428f9687e11c23ca83d778cc97` |
| `src/language/semantic_calls.c` | `4411c53dc968e2d982f310e6a5b63d1b26bdc2ea0a4a1aaa9bb7f83760a1eb35` |
| `src/capabilities/catalog.c` | `a8ec7c2c74f2229b0768c297430db59d05d11c001dd28eeeb19e1c93602037bf` |
| `src/authoring/source_catalog.c` | `2c65a4cedc5be72f9bc0396e801a86218d5a21136f6ae0fcae9efade262e12fe` |
| `src/authoring/source_catalog_snapshot.c` | `c4a4ed13792c2f34834d74b0d83fe87d9d557fdf4cd6cabb80844777cbd8e493` |
| `src/authoring/source_package_prepare.c` | `860616c58fa89914ab80018113cc7f34d9b6d18570118bb1b4db1166cd0fcd6c` |
| `src/authoring/source_package_snapshot.c` | `1bb7f08e090f2f189c1b84e48a6dddbc1335285368528f27b591540c744979af` |
| `src/host/build_host.c` | `ca711ba7b48da5a380d990a4ca650ddd5da428440068de9e3303ac5846ea3450` |
| `packages/core/strings/Strings.chibi` | `4ece97a79861c9c14fd47184c99c69918a87b8a250394b9667cef53bdedf05fe` |
| `sdk/CMakeLists.txt` | `30c14df43f34231ec1e45cd1276a7a3e5cc9acace613b1c866b4bddf997b3efe` |
| `sdk/ChibiConfig.cmake.in` | `0724bd2ceadfaa9e0aee8d3c2a6da4b8112147c3b61e87824299b1e9029c61b8` |
| `sdk/execution.cmake` | `f34e880d563986fcb461a2332f468f77ca16aa73a97a14ef6c7eaa0d86011880` |
| `sdk/contracts.cmake` | `a6789b8c7af12245f76819cb08a3cb88b9d5f64f5d4721f1a3dfbf8f19b16ea8` |
| `sdk/values.cmake` | `9931c6c13e1bc002e734f990356a7d19f5015b4ad2b15abfcfc21190d90d86e3` |
| `sdk/source_authoring.cmake` | `6f4884681d319de5eb31850980675792b2434da97929c7f50d821432af736762` |
| `Makefile` | `eddd01a570e299bf289ac70fc895298b19e3ad57ef76f7a7a1d6ef1a4476a387` |
| `test/language/authority.c` | `e7e08b4fbc21bb252f5cdaf176fc8987afcd4c4ab6e0f6008a28c7b4a01ce6eb` |
| `test/language/scalar_append.c` | `13e3a724b3cf310255d9a685368bd542e8c96a363e8b2536ac6d2d441f341e0a` |
| `test/language/outliner_work.c` | `9c2af9b1ab45cf5aae5a8d9cda33fbf6856fdee3acff9164267c5d7409aa03b2` |
| `test/language/package_execution.c` | `3084f63d3a43b6a9c5c24543827581214a30950a82076e07d9ea90f748b291c1` |
| `test/language/package_imports.c` | `1e04841bb6664ae1a1f4786f48ccc68c6375503b46276b4ad2b4a2f7d6105c2c` |
| `test/language/record_publication.c` | `1f82a758f927e900ec7c67584a2075d9f8b6bfb4f18964ec56338a703b81019a` |
| `test/tools/request_work_c.c` | `b961d70ef07ffa8973b2ef48739e85af2c7ab025a2158ccabf1c5874f3010748` |
| `test/tools/build_process.c` | `c0980dff3fe36b46177edaac1c183c494b6a152871c7f228762ee07302f9586b` |
| `test/tools/package.c` | `734c514989a11f3d292156decf1d0ce3b81295b3caf19584a5d37bd12ad63cda` |
| `test/tools/package_ownership.c` | `cf2e2b0a2aff81d0c0cb16d9b9fd11a521c3666b542898d041a446405a88470a` |
| `tools/package_sdk.py` | `ab0a4eb527103db05ca2adae335cc10e32ad9600bab88c1433bcd6f1a9227ee7` |
| `tools/test_policy.py` | `0722c79b668bdb51e4db6ee71418754653b21fe9965c3bf4cea4c762dbeaeb8e` |
| `docs/testing.md` | `8d9db2e96486d5da55d884bc112801fa96d5158d7da95b07fe8ca63cea471310` |
| `docs/toolchain-inventory.md` | `450b1ca615ea88f3b493f32ae585b54f9613017a8213279edc6e33f095920440` |
| `PLAN.md` | `505597050ed12e5e64e980ec1978da0a62153f4207a66eff066cc8c5e6a1be7c` |
| `tools/dependency_package.py` | `7ec78520c526e918b38ffa15e9aeed1cde1f10ee8ac6d13a1220ed31494c354a` |
| `cmake/tests/package_ownership/CMakeLists.txt` | `fa62defb2576a60c540077dc538488ba68935db7347c5c863195c5a892224c18` |
| `test/language/CMakeLists.txt` | `8fa0b4cd30cd36afad9e5e667dac7b6b18b0900501a53f06038ff2e2cb81c209` |
| `test/language/package_core.c` | `9bf1aae878032fa76648b4e8e4037548c7924b47c2d7cfcc37d81249e9f8be07` |
| `test/language/package_core_source.h.in` | `9796f2a0eb851445a5f5facbd2ebbd3397aebee3a2c7c29f414103ab2634a0da` |
| `test/language/package_resolution.c` | `bf255ce6fdd0a03ca19bb51b20ce6e40a4efda76a550bbe32c599718d6bab548` |
| `test/language/package_files.c` | `a9ecc178903b660c97e3ec67db8e7c59732d55a1f86522f97dc129345f0affb9` |

## Retained evidence hashes

| File under `/workspace/outliner-build` | SHA-256 |
| --- | --- |
| `test-request-work-3.log` | `ce516c3cc65621ea24958edd1cafd32a4796844f3cedb1941deb009f0c42bb29` |
| `test-wasm-request-work-1.log` | `c733aa6fa4cfa663098a5e908ad8a814ba51959fa22bc1f3ae90f21c275a062f` |
| `test-request-work-c-1.log` | `57abaa8ab2af1fe3f119bd741c71443cc738ccf35adca0b7433ab9eb318c6b96` |
| `test-scalar-append-5.log` | `d82d1248a16805506f777e4eb800d1fcf31afc228c8d7a5d2fbc5f837c13d3fb` |
| `test-wasm-scalar-append-1.log` | `429ed76d720b531b6d1bedb7e0541f91d891c1ea93d0c2ccd8d18f73d517b089` |
| `test-outliner-work-3.log` | `530ceb49ea65144694a986b57b26829d430725c24dab3f7663617388f80ecc70` |
| `sdk-native-build-10.log` | `c5212a92eca4cb4108fcf2879fc3b48fbdca7e0ee7f444423eb3190ad5011254` |
| `census-final-abi-review.md` | `29e04a2eafc5dc0786dc5ac64ad74d7176c801dacb04517b8856864d0a52a8e5` |

Snapshot written 2026-10-18T06:47:33.752502+00:00.
