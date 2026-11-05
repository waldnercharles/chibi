# Full-width POSIX file stamps: source checkpoint

`core:sys/posix` decoded stat fields into the 32-bit `int` type and passed those
values through scalar formatting. Inode, size and timestamp words can exceed
that range. The correction widens each input byte before shifting, retains the
full unsigned word, and renders the same field signs and widths as the C host's
`build_io_stamp`.

- Device and inode use unsigned 64-bit decimal. Darwin's signed 32-bit device
  value is sign-extended before its unsigned conversion, matching C.
- Size and timestamp seconds use signed 64-bit wrap interpretation, including
  the minimum signed value.
- Mode remains unsigned octal; valid nanoseconds have exactly nine digits.
- Public `Stamp` still follows links and returns an empty string when stat fails.
  Windows and unrelated filesystem operations are unchanged.

The private pure transform has byte-record regressions for 2^31, 2^32, 2^63,
u64 max, signed minimum, short/nonzero-offset reads, both platform layouts,
unchanged input and repeated output. A separate public consumer compares tiny
real-path fixtures with Python's integer-nanosecond stat results at O0/O2.
No large or sparse file is needed.

Independent source review found and corrected two CI fixture omissions (the
new stamp runner and the preexisting source-audit runner) plus stale option-order
expectations left by the earlier build-entry delimiter correction. The production
build-entry behavior is unchanged. Its four isolated launcher fixture cases pass,
with no skips and no actual native compiler or proof program execution.
The first two launcher runs failed on those stale expectations and remain recorded
as failures, not discarded repetitions.

At this source checkpoint, pure-data reference and shell/Python syntax checks
passed, but did not execute the changed Chibi code. After compatible compiler
recovery, the complete unchanged Linux O0/O2 synthetic and actual C/native/Python
suite passed and was independently accepted; see [POSIX-STAMP-VALIDATION.md](POSIX-STAMP-VALIDATION.md).
Darwin execution and affected Graph/Driver/product/integration validation remain
separate. This focused result does not imply merge readiness.
