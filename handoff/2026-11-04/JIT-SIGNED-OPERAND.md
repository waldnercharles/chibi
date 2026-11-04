# Strict JIT failure-return operand correction

The next fresh compiler-host construction advanced past the fixed native-default
initializers and produced 44 successful C objects. It then stopped on one GCC 14
`-Werror=sign-compare` diagnostic at `src/execution/jit.c:207`: an `offsetof`
branch made the conditional's negative error-code branch unsigned. This failed
construction produced no archives or host. Its Graph command took 19.962177
seconds within the unchanged 180-second phase bound; this is an early-failure
diagnostic, not a complete-build benchmark.

The correction casts only that offset arm to `sljit_sw` before the conditional's
usual arithmetic conversions. The API already takes that signed word type.
The nine failure-reason mappings, negative status values, command-status memory
load, `SLJIT_MOV32` return width, context layout and ownership are unchanged.
The fixed concrete member offset fits the supported conventional 32/64-bit
signed word models. No exact cross-platform layout or runtime result is claimed.

## Focused regression

The complete production translation unit was compiled twice with the same
GCC 14 GNU23/O3, `DNDEBUG`, strict warning and section flags. No warning was
suppressed or lowered. The published baseline failed with exactly the original
signedness error and produced no object. The one-line candidate passed with
empty diagnostics and a 41,720-byte x86-64 ELF relocatable object. Both commands
matched the exact ordered 118 dependency occurrences after isolated path rebasing.
This actual strict-unit compile is the regression that fails without the fix;
there is no copied-expression or mock-emitter test.

The two commands took 0.504783 and 0.497560 seconds. These are bounded correctness
observations, not a speed comparison. The entire phase took 3.635952 seconds
under its original 60-second cap, with 15 seconds per compiler and five seconds
reserved for owned cleanup. Source/tool/include/Python/search fences passed,
and cleanup reached authoritative ECHILD with no signals, errors or survivors.
A hard per-file limit and live log/aggregate/count/free-space checks remained
enforced; sampled aggregate limits are not a filesystem quota.

Source and exact terminal readback are independently accepted. [Machine-readable results](jit-signed-operand-results.json) retain
the exact source, object and receipt hashes.

## Remaining gates

The object has not been linked or executed. A separate small generated-code
semantic proposal remains disabled and is not used to claim runtime coverage.
A fresh full-host construction, host inspection/execution, complete runtime and
engine integration, actual Windows and exact-head CI remain open. The original
full graph-c and other explicit holds are unchanged. PR #1331 is not ready to
merge. No release or seed pin changes are included.
