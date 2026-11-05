# Census architecture checkpoint: inactive source archive

This checkpoint preserves unfinished architecture work. It does not activate code,
change defaults, replace historical checkpoints, or claim merge, release or CI success.
The compiler changes are an unapplied patch against the exact base tree in identity.json.
Do not apply it to a current checkout without a separate integration review.

## Current state

- Revision04 requests Model v4, while generated bridge artifacts remain v3.
  MaxBlocks is 64 and the accepted v4 digest is unassigned.
- Five fresh Python prerequisite stages passed: 14 preparation tests; 14 ordinary
  oracle tests with five explicit large skips; five separate large oracle tests;
  intended coherent-capacity mutation rejection; exact 28-file preparation.
- Native typed-suite compile passed in 18.754858685 seconds. Its 17,657,120-byte
  ELF passed independent artifact review. The once-only typed execution then
  failed with SIGSEGV (exit -11) after 0.425247585 seconds, with empty stdout and
  stderr and closed ownership. No typed-suite acceptance. Cause remains unknown;
  separate diagnostic preparation does not establish a fix.
- No v4 generation, backend build or Census native execution is accepted or run.
- Native harness03 is source-only: first four review passes and targeted fixes
  were completed; remaining review and all preparation/native execution are open.
- Work counters02 and stdout adapter are source-only. Final independent review,
  source transformation, compilation, output capture and acceptance remain open.
  All work counts are unknown, not measured zeros or predictions.
  The frozen counter02 proposal keeps WorkOutput.chibi as a separate root source,
  but native_driver.py --source accepts one file. Helper integration is unresolved;
  this is not a runnable or validated counter configuration. A fresh source-only
  counter03 is being prepared separately and is not included in this archive.

## Preservation and deliberate exclusions

Source and tests in both archive directories retain exact authored bytes. The
technical READMEs are retained as AUTHORED-CONTEXT.md with a non-executable archive
notice; identity.json records original and archived hashes. OUTPUT-SCHEMA.md is
retained unchanged. Private reviews, author scratch reports, stale hash lists,
raw environment receipts, execution packets and executable binaries are excluded.

The native source preparer still names AUTHOR-REVIEW.md, which is intentionally
not shipped. Its exact source is preserved, not silently edited to remove a
provenance requirement. This archive is therefore not a self-contained runnable
harness. A future runnable package needs separately reviewed public provenance,
current source pins and generated-v4/backend artifacts. Nothing here is wired
into production imports, selectors, launch commands, CI or defaults.

## Evidence boundaries

validation.json normalizes existing receipts rather than rerunning them. It
preserves earlier packet02/packet04 failures and packet03's unexecuted status.
The archive operation only verifies file hashes and patch replay with whitespace
errors rejected. That proves preservation, not language, ABI, performance or
native correctness. Historical authored context may contain earlier expectations;
this status and validation.json govern the current acceptance state.

The preceding remote checkpoint was commit
227665c5177597dee7361393cd11ea9e36f3aa0e on
codex/vnext-compiler-architecture-20261104. It preserved the original inactive
patch and accepted five-case v3 replay. This newer checkpoint is additive and does
not overwrite or retroactively strengthen that historical evidence.
