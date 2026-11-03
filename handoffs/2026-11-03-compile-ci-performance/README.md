# Compile and CI performance WIP checkpoint

The user stopped the investigation on 2026-11-03 and requested only a commit, push, handoff summary and task list. Read [handoff-summary.md](handoff-summary.md) and [task-list.md](task-list.md) before resuming.

This branch preserves the source tree at `bc6d971d33be36a8d7f8dd32b86253d9fe7f46e5` plus this handoff. Other local investigation tips are retained as archive merge parents. Their histories and exact trees are recoverable; their divergent implementations have not been combined into this source tree. This is a WIP archive, not a proposed production merge. `source-refs.json` maps every retained local investigation branch to its exact commit and tree.

Evidence is archived under this directory. Reports and scripts describe distinct frozen source epochs. A later report may correct an earlier count or qualify an earlier result. Follow the final timing checkpoint and the source-specific receipts.

The original evidence directory remains intact at `/Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/evidence`. Generated binaries, LLVM/object outputs and third-party Git metadata are local artifacts. The evidence inventory records which files are included and which remain local. No source checkout, local artifact or other owner's process was deleted.

## Evidence recovery

The archive preserves43,968 textual reports, scripts, fixtures, receipts and logs,455,266,773 original bytes. Its three compressed parts total102,970,757 bytes. `evidence-inventory.jsonl.gz` records SHA256, original size and archive/local disposition for every regular evidence file. Thirteen machine-wide process captures stay local. No credential or signed-URL match required redaction. Third-party `.git`, `node_modules` and Python bytecode directories are excluded from the archive.

Verify the part hashes and the combined archive hash in `evidence-archive.json`, then extract in a fresh directory:

```sh
cat evidence-text.tar.gz.part-* | tar -xz -C /absolute/path/to/new-empty-directory
```

The archive contains an `evidence/` root. The readable `reports/` copies provide the main timing, fixture and prior-art findings without extraction. Large generated LLVM, embedded byte arrays, compiled products, media and binary caches remain intact at their original local paths and are inventoried. Recovery of those local generated products depends on the original machine or rebuilding from the pinned source/tool receipts; this branch does not contain those binaries.

## Checkpoint validation

The new commit adds handoff material only. Existing implementation and independent-review receipts are preserved. No new implementation, experiment, test, CI dispatch, publication or pin update was performed for this checkpoint. Source tips are retained as archive merge parents without merging their source trees.
