# Final-ABI Outliner census qualification — 2026-10-18

Independent reviewer: `/root/outliner_contract`. Read-only inspection of the
engine/Studio sources, configured header copies, compilation records, frozen
engine manifest and the parent's recorded runs. Only this outside review note
was written; no build, preparation, authored execution, test or shared-source
edit was performed by the reviewer.

## Finding

**K = 249 is qualified for the exact integrated request-fuel ABI and shipped
source identities below.** The census measures canonical static BF_CHECK sites,
not optimized runtime instructions, observed top-frame steps, native work or
wall time. All14 frozen fuel-engine file hashes match the actual engine tree.
Configured public/private context header copies match the source header.

The parent reports the54.0s native build completed before the census. The build
log and compilation records identify the concrete driver/configuration below;
the name “native22” is only a human ledger label, not sufficient provenance.
The census run passes1case/393assertions in0.475s under the60s supervisor.

This qualifies the coefficient independently of runtime execution correctness.
The generated-C fuel run passes1case/107assertions in0.313s, but request-work's
separate runtime fixture is still under lifetime diagnosis. Neither the census
nor the C result authorizes dismissing that failure or claiming all backends
meter correctly. Studio installation/activation still follows the owner's
integration freeze and review decision.

## Exact source and artifact identities

All values are SHA-256.

| Input or evidence | Hash |
| --- | --- |
| Studio `projects/studio/content/behaviors/OutlinerRows.chibi` (1441 bytes) | `510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450` |
| Studio `projects/studio/content/libraries/OutlinerRowPolicies.chibi` (11668 bytes) | `e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5` |
| Engine `src/language/lower.c` | `2658fc232b983e2d91a983998e3ad5e306963a428f9687e11c23ca83d778cc97` |
| Engine `src/execution/function.h` | `fe1e9b69f923f7169f7731e78adecb7325d03b1a98dc12a60ce58b84bc50e09c` |
| Engine `src/execution/prepared.c` | `cfec73fa439511c5d7fa368ad646e1b35cd0d01ed5ed74c56c57fc85924512a8` |
| Engine `src/execution/row_builder.c` | `e5f379006f46b3df4d15f97e4aa5396b73dcc65926878b803a4fcdfb605eb520` |
| Engine `test/language/outliner_work.c` | `9c2af9b1ab45cf5aae5a8d9cda33fbf6856fdee3acff9164267c5d7409aa03b2` |
| `request-fuel-draft/ENGINE.sha256` (14 integrated fuel files, every entry checked) | `95af69dc75b23505bae0ebec0d31c3860f47aa8e1c6a8f269c32bf5be7b3a8b1` |
| `test-outliner-work-3.log` | `530ceb49ea65144694a986b57b26829d430725c24dab3f7663617388f80ecc70` |
| `native/test/language/test_language` observed after this run | `6b1602cb347ccc3bc44581b4b854a6a8b57cce2960a994bb5999dac90d881aa6` |
| `native-build-22.log` | `87ba0ee6fc7f1a6f21050482a26709390d195656c2b1fc66a40c9041c3a2a8ab` |
| `native/compile_commands.json` | `6238345ac3b77e3f37a903df559d451e2a73f5d9b8417ad527d6ae9cc9d12d90` |
| `native/CMakeCache.txt` | `9b607a480d7b08bc5441e777c1d505841c64a3d9454cb645534eb0fa5c48221f` |
| Separate `test-request-work-c-1.log` | `57abaa8ab2af1fe3f119bd741c71443cc738ccf35adca0b7433ab9eb318c6b96` |

Artifact paths in this table are relative to `/workspace/outliner-build` unless
explicitly identified as engine or Studio source paths. The context source hash
also matches both `native/sdk/public/execution_contract/execution/function.h`
and `native/sdk/private/execution/execution/function.h`. The private work header
matches manifest entry `a3e1508d62da99923c83cc0682a8779b654d2ffb63fa4035ede5349762de8371`.

The actual native compile records for prepared.c, machine.c and outliner_work.c
use Clang19, Debug, ASan+UBSan and frame pointers; the fixture also has `-UNDEBUG`.
The context declares a borrowed `execution_work *work` and a two-uintptr_t
spent/limit balance. prepared.c fingerprints the balance size and field offsets
as well as `behavior_function_context.work`. This is the new ABI implementation,
not an older context header accidentally retained in the configured SDK copy.

The fixture does not print a numeric wire ABI fingerprint or serialize a
prepared image. Therefore this review claims a census prepared with the pinned
ABI implementation, not a separately measured prepared-image round trip. Final
package provenance must still add the clean engine/Studio candidate tree IDs and
the installed SDK build/source-snapshot identities when those artifacts exist.

## Closure and sum checked

The fixture constructs the reviewed minimal Outliner+Array+Text native catalog
with exact native records/entity metadata, prepares one bytecode collection from
seven fixed overlays, checks the20-body inventory and actual imported-module
pointer, and follows actual synchronous callee pointers from Refresh/Reveal.
It rejects unknown bodies/tasks and requires zero prepaid root_steps. The run
reports exactly22 compile-time constant evaluations and no runtime invocation.

| Reachable body | Canonical BF_CHECK sites |
| --- | ---: |
| OutlinerRows.Refresh | 6 |
| OutlinerRows.RevealObject | 7 |
| Depth | 9 |
| Icon | 17 |
| Drop | 49 |
| SelectState | 4 |
| FoldByte | 7 |
| ContainsFolded | 21 |
| LastIndex | 21 |
| Title | 13 |
| IntRange | 9 |
| BehaviorCount | 12 |
| Page | 21 |
| Project | 53 |
| **Total:14 bodies,5035 canonical instructions** | **249** |

The six other controller handlers remain excluded. Both request handlers are
included conservatively although only one runs per request. All12 library
helpers are reached. Project derives two append plans and IntRange one; those
optimizations do not alter this canonical count.

The independently reviewed source multiplicity gives
`W = 8 + 3N + 8V + T + 3R + D + 2Cv + 2Fv + Av + l + min(l,127)`.
Consequently `10000 + 249*W` is the qualified conservative absolute allowance
for this exact shipped algorithm, with every intermediate checked and spent
work preserved. It is not a bound for arbitrary safe edited policy completion;
such policy can exhaust the same finite allowance normally.

## Durable Studio provenance recommendation

When the owner releases the Studio freeze, keep a small in-repository evidence
bundle, for example `docs/evidence/outliner-request-work/`:

- `census.md`: this identity table, the14-body count table, formula argument,
  preparation/catalog/backend/configuration description, qualification limits,
  final engine/Studio clean tree IDs and installed SDK build/source identities.
- `census.log`: exact bytes of the qualified `test-outliner-work-3.log`, keeping
  its SHA-256 above. Keep the observed log rather than rewriting it as a summary.
- `engine-fuel.sha256`: exact14-entry manifest from ENGINE.sha256, plus a small
  separate source manifest for the two authored inputs, lowerer, context header,
  prepared ABI implementation, row builder and census fixture above.

The evidence should name the reproducible route
`CHIBI_LANGUAGE_BUILD_DIR=<final-native-driver-directory>` and
`CHIBI_OUTLINER_CONTENT=<absolute-Studio-content-directory>` with
`python3 tools/test_policy.py tool language-outliner-work`, and retain the actual
invocation/configuration record. Do not pretend placeholder paths reconstruct
the historical command or that a workstation label is a build identity.

Place a concise comment immediately beside the production constant, linking
that repository-relative evidence and naming the inputs; for example:

```c
/* Frozen shipped closure: docs/evidence/outliner-request-work/census.md.
 * 14 reachable canonical bodies, 249 BF_CHECK sites; census.log SHA-256:
 * 530ceb49ea65144694a986b57b26829d430725c24dab3f7663617388f80ecc70
 * Authored source, lowerer and context/prepared-ABI hashes are pinned there.
 * Requalify after changes to those inputs or checkpoint lowering/accounting.
 * These hashes document the bound; producer validation grants authority. */
#define STUDIO_OUTLINER_CANONICAL_CHECKPOINT_WEIGHT ((uintptr_t)249)
```

The adjacent durable document supplies the full sourcehash/lowerer/function.h/
prepared.c identities rather than crowding six64-character values into code.
No build knob, dynamic hash-based authorization, guessed fallback or native
row-policy reconstruction is needed. Preserve the actual prepared-producer
validator and current request authority checks. Source hashes remain evidence,
never publication authority.

## Conditions on using this qualification

The coefficient no longer needs to wait for an identical census merely because
the label “native22” changes. A fixture-only lifetime correction does not change
K. Any source, catalog/preparation, lowering, checkpoint placement or context/
prepared ABI change must be reviewed against this evidence and, where affected,
recensused. A runtime implementation fix that leaves the canonical census intact
still needs its own execution review/validation; the coefficient cannot cover
an accounting defect. Final SDK and Studio builds must consume the qualified
source/ABI or a separately requalified replacement. The pending runtime fixture,
Wasm, Studio live-fuel and final clean-candidate checks remain separate gates.
