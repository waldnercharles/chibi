# Resolved ordinary Studio main merge

Status: authorized ordinary merge in progress, all conflicts resolved, not committed. No build/test/application execution performed in this task.

- Actual HEAD: `521b666a62406fabae8e1d23bad00765717b3635`
- Tested checkpoint tree: `19c1bbfb599f7c8a6a0ba92e063b37e60c078524`
- MERGE_HEAD: `df050886dd7721db63b40fdf5435f1cb20ceeb34`
- Final staged tree and refreshed candidate: `3352628508469dbce5db9c24bf0b7df027f124e7`

The candidate was refreshed before beginning the actual merge. Twenty conflicted files were restored directly from actual HEAD; main.c retained actual HEAD and added only the eligible SDK include, dispatcher and help entry. The automatically merged README retains the final model261 PASS6.174s evidence plus incoming SDK documentation. New sdk_values.h is the exact incoming blob. Production and EXTRACTION have no staged difference from HEAD. The pending merge adds exactly three acceptance paths: README.md, main.c and sdk_values.h.

All 476 extraction hashes match actual contents (258 consumer, 218 source fallback). The graph SHA remains `233e86db49b97f118894ccc13d6f5ec8559e71a4953b65d9b760d7e2d5da8ec8`. No retired path/reader/dependency matches were found. There are no unstaged changes or unresolved index entries; staged whitespace check is clear.

`actual-merge-receipt.json` records exact source-state guards and audit counts. `state.json` records all immutable resolution blob/hash identities. `candidate.patch` is the checkpoint-to-result delta; `conflict-resolutions.patch` resolves the mechanical merge tree. `result/` has the resolved files. Historical candidates536ea9 andfcc7ed retain their metadata and README evidence under `historical-536ea9/` and `historical-fcc7ed/`.

Independent source review cleared the previous equivalent three-path candidate; refreshed production differences came directly from the parent-tested checkpoint and are preserved exactly. Parent review of this actual pending merge remains the next step. Earlier model261 evidence does not validate the newly combined SDK selector or imply final cold/Web acceptance. No commit, abort, stash, push, build or test was performed.
