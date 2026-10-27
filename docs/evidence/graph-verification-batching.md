# Graph verification startup: invalid-profile batch

## Scope and dependency

This candidate is based on PR #730, composed source
`99ff8ca727447d584a87218963dd19ddc17972ac`, which includes #729 and the
reviewed macOS temporary-path fixture repair. The initial
startup investigation used #730 source `7f4507a6`. **Do not merge it until #730 has
passed its independent review/validation gates and landed.** It is separate
from #729's preserved asset-generator changes and #726's sharding work.

The motivating Linux fast-CI Graph command-parity step took 170.30 seconds;
the functional groups took 172.20 seconds. Those are whole-step observations,
not a measurement of compilation alone. No candidate speedup is claimed here.

## Source-level work count

For `--build-graph-debug-optimization`:

| Work | Before host/Graph compilations | After host/Graph compilations |
| --- | ---: | ---: |
| Explicit CMake command parity | 1 | 1 |
| Optimization × sanitizers × frame-pointer matrix | 18 | 18 |
| Fourteen invalid argument cases | 14 | 1 |
| Real CLI missing-value rejections, one per option | Included above | 2 |
| Unexpected batch result stops the next case | — | 1 |
| Transcript transport unit assertions | — | 0 |
| Total | 33 | 23 |

The candidate evaluates Graph 36 times: all original 33 evaluations, two
additional genuine CLI rejections, and one deliberate unexpected-result
evaluation. The latter must stop before evaluating its second case. These are
source-audited counts; hosted execution remains the runtime verification.

## Preserved contracts and bounds

`Libraries()` remains the production CLI entry. Its original body is unchanged
except that `build.arguments()` is supplied through the private
`LibrariesWithArguments(string[])` seam. The test appends a fixed verification
entry to a temporary exact copy of that source; there is no new public CLI mode,
compiled-program cache, host API, or developer-default change.

All 18 valid command-generation cases remain isolated and source-equivalent.
Only explicit-profile invalid cases share compilation. Each rejects before
discovery, command generation, output writes or child processes. Each keeps its
own argument slice, exact status/diagnostic interval and unique absent output
directory. Empty values, repeated values and missing final values are retained.
The real CLI missing-final-value cases require exit status 2.

The fourteen-case payload uses 172 host arguments (limit 256) and its expected
diagnostic transcript uses 1,555 bytes (capacity 4,096). Existing argument-byte,
text, array, 256 MiB native-value storage, instruction, 50-second host and
50-second fixture deadlines are unchanged. Existing process-group ownership and
cancellation remain in `tool_run_within`. Valid generation is deliberately not
batched because native values persist until host teardown.

The added regression requires an unexpected authored result to return status 1
and an exact first-case-only transcript. Pure transcript checks reject skipped,
reordered, stale-diagnostic, wrong-status and trailing cases.

## Validation status

Local source checks: strict GCC C syntax with `-Wall -Wextra -Werror -Wpedantic
-Woverlength-strings -UNDEBUG`; clang-format 22; and source-equivalence comparison
of the composed Graph body and valid matrix. The exact extracted pure C
transcript fixture passed one case/seven assertions with the same strict flags.
The composed policy fixture passed 47 cases/617 assertions, including the
parent's temporary-path regression, with the same strict warnings.
A full-file scratch GCC wrapper encountered pre-existing unused-function and
format-truncation warnings in unrelated functions; those warnings were not
disabled. Full-driver compilation remains a CI gate. Independent source review
found no blocking issue. These checks do not prove authored compilation or
execution.

Required next evidence: normal/manual CI on the reviewed exact candidate, with
the Graph selector passing and before/after elapsed time compared within the
same CI profile. Hosted RSS/limits, genuine CLI behavior, CMake parity and broader
functional integration remain unverified until those runs complete. Do not infer
wall-time savings directly from the reduced compilation count.
