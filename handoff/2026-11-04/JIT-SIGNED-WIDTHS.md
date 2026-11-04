# Preserve signed JIT spill operands

Two complete-product casts in `jit_memory.c` and `jit_registers.c` keep spill
byte offsets in SLJIT's signed machine-word type before conditional selection.
They retain the original `size_t` multiplication and selected evaluation, with
no new branch, allocation, record field or layout change. Casting the index
before multiplication would still leave the unsigned promotion; changing the
multiplication to signed arithmetic would change overflow semantics.

Independent full-file and caller review accepted the two-line correction.
Existing valid slot/frame ranges fit the signed word on the supported ABIs;
this is not new validation of malformed IR, oversized frames or every platform.

## Exact strict compile pairs

Each complete original unit reproduced its one expected signedness error under
the unchanged GCC 14 GNU23/O3/NDEBUG warning-as-error recipe and produced no
object. Each corrected unit compiled with empty diagnostics. All four commands
retained their own exact 116 ordered dependency occurrences. Independent
readback checked source and tool identities, command records, logs, objects,
dependencies, timestamps and clean child cleanup.

The accepted corrected objects are 22,520 bytes (`jit_memory`) and 19,888 bytes
(`jit_registers`). Their ELF section, symbol and relocation structures were
checked read-only. Neither object was linked or executed. Exact source, artifact
and proof hashes are in [jit-signed-widths-results.json](jit-signed-widths-results.json).

## Failed broader diagnostic, preserved

The same bounded campaign continued through the remaining strict C units. It
ran 43 of 44 planned commands in 14.591093 seconds, then failed closed because
`binary_cache.c` reported the canonical dependency spelling
`/usr/lib/linux/uapi/x86/asm/errno.h`, absent from the permitted path map. The
file's contents and identity match already fenced aliases; that does not
retroactively admit the missing spelling or pass this command's dependency gate.

The exact outcome is 38 validated candidate compiles, two ordinary source
failures, one exit-zero but unvalidated `binary_cache` compile and unrun
`compiler_main`, plus the two expected original-unit failures. The source
failures are four mixed signedness expressions in `jit_suspend.c` and two
pedantic object-to-function-pointer casts in `runner.c`.

The owner reached clean ECHILD with no errors, signals or surviving children,
within the unchanged 180-second total and 15-second command limits. The worker's
success-only final checks did not run. Independent postfailure checks instead
verified all immutable inputs and retained artifacts. This supports the two
fully recorded strict pairs independently; it does not turn the whole campaign
into a pass. The failed packet remains consumed, with no retry in place.

No archive, link, Graph build record or host was produced by the diagnostic.
Full host construction, real runtime parity, full graph-c, actual Windows,
exact-head CI and PR integration remain separate gates. Timings are bounded
correctness observations, not performance measurements. PR #1331 remains not
ready to merge.
