# Preserved checkpoint evidence

These files were copied from the original validation workspace. They describe
the paused working source and are historical evidence, not new runs or merge
approval. Start with [the handoff](../../live-outliner-handoff.md).

Logs are losslessly compressed as `*.log.gz`; decompress to recover the exact
original bytes, including diagnostic whitespace. `ORIGINAL_LOG_SHA256` records
the uncompressed identities used by the independent reviews. `SHA256SUMS`
covers the files stored here (except itself). Absolute workspace paths and
original log filenames in the reports identify their historical inputs; the
new workspace must rebuild artifacts and adapt paths.

The copied PAUSED.md records the earlier pause. The user's subsequent request
authorized committing and pushing this checkpoint, without resuming builds or
finishing the pending validation. Final runtime, integration and Studio review
notes supersede earlier provisional notes where they differ.
