# Existing preparations: UNVALIDATED WIP archive

This is a public source-preservation checkpoint, not a portable executable proof, completed implementation, or passing validation report. It preserves existing work without applying the pointer patch or changing implementation. Publication does not enable execution or establish independent review, merge readiness, Windows acceptance, or successful host compilation.

## Contents and limits

- `graph-c/`: 41 exact original small text files containing disabled controllers, finite controls, source/provenance records, plans and historical checksum files. `controls/PUBLIC-REVIEW.md` is a separately hashed derivative retaining technical findings. The two small counterfactual core copies are intentional controls.
- `pointer-view/`: the exact unverified patch and separately named sanitized metadata. Base revision is `02c86287430def964445ec90185961e20319536d`. Its six tracked modifications and nine new source/test files were preserved in the patch; the patch was not applied. All 15 live-file hashes matched the saved metadata at inventory time.
- `native-default-initializers/`: the exact source-line audit and a technical diagnosis derivative. No initializer implementation exists in this snapshot.

Graph runtime `plan.json` retains `runtime_enabled: false`; `controls/CONTROL-PLAN.json` retains `enabled: false` and null fixture/source/artifact/execution admissions. Status remains `FROZEN_SOURCE_ONLY_UNARMED`. There is no synthetic fixture ELF or admitted/executed control suite or graph runtime proof. Independent reviews, runtime/source/tool closure checks, diagnostic-policy validation and actual terminal evidence are missing.

The pointer metadata retains known findings: guarded nullable pointer inference can lose ordinary narrowing and produce incompatible native array identity, and the generic Failure assertion oracle can accept the wrong assertion site. Patch reapplication, ordinary/native differential, native compiler/runtime tests, reload and Windows acceptance remain unrun. The pointer union-schema rejection fixture belongs to this patch and is not the excluded union native16-byte leak investigation.

## Deliberate omissions

No duplicate project source tree, 1.59 MB `source-baseline.json`, binaries, object files, generated programs, caches, archives, instructions, or unrelated notes are included. Required external source/tool/runtime inputs are not bundled. Absolute paths in exact originals are historical references and may not exist on another machine. Do not weaken or rewrite the disabled gates, pinned hashes or missing-input checks to make this archive run.

The original graph checksum files intentionally remain unchanged and may reference omitted files or the original unsanitized review. They describe the original packet, not this subset. They are not newly issued acceptance evidence. `COPIED-FILES.json` distinguishes exact copies from derivatives and newly authored archive documentation. `ORIGINAL-INPUTS.json` records original input byte counts and SHA-256. The top-level `SHA256SUMS` inventories this public archive, excluding only itself.

Only text copying, metadata inspection and hashing were performed to prepare this checkpoint. Original packets remain unchanged. No implementation, project import, compiler, test, controller, native program, or runtime validation was executed as part of archival preparation.
