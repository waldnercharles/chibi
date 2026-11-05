# CW1 stdout contract (version 1)

UTF-8/ASCII records separated by LF, fields separated by one literal HT (tab).
Every line begins `CW1`. No blank lines, CR, leading/trailing whitespace or extra
stdout are admitted. Integers use unsigned base10 digits, no signs, separators,
padding or units. Counts fit nonnegative Chibi int; write masks are u64.
The strings below use TAB to show a literal separator; angle labels describe
values rather than emitted syntax.

Exact record sequence:

1. `CW1 TAB begin TAB <actual typed.MaxBlocks>` once.
2. `CW1 TAB groups TAB producer TAB <length(report.producer)>`
3. `CW1 TAB groups TAB verifier TAB <length(report.verifier)>`
4. `CW1 TAB groups TAB census TAB 1`
5. `CW1 TAB skipped-count TAB <length(report.skipped_blocks)>`
6. All producer records in zero-based returned-array order.
7. All verifier records in zero-based returned-array order, each preceded by
   `CW1 TAB label TAB verifier TAB <ordinal> TAB <label>`.
8. Census record at ordinal0.
9. For each actual skipped row:
   `CW1 TAB skipped TAB <ordinal> TAB <block-size>`.
10. `CW1 TAB report-end` exactly once.
11. After the entire former Main regression body succeeds:
    `CW1 TAB regressions TAB passed` exactly once, as the final stdout line.

For each counter record:

`CW1 TAB count TAB <producer|verifier|census> TAB <ordinal> TAB <field> TAB <value>`

Exactly these fields, in this order, each once:
prepass_statements, assignment_resolutions, mask_updates, emitted_statements,
expression_entries, emit_calls, while_conditions, while_bodies, parser_nodes,
reachability_pops, dominance_pops, predecessor_reads, word_intersections, functions.
The first13 values are the returned WorkCounts fields. `functions` is the actual
length of its functions array, not a source projection.

For each function row, in zero-based array order, emit:

`CW1 TAB function TAB <category> TAB <counter-ordinal> TAB <row-ordinal> TAB <field> TAB <value>`

Exactly these fields, in order, once each:
function_id, complete, blocks, values, instructions, edges, arguments, parameters,
signature_parameters, assignment_rows, loop_rows, write_masks.
`complete` is0 or1 from the returned boolean; `write_masks` is its actual array
length. Next emit each mask in zero-based array order:

`CW1 TAB mask TAB <category> TAB <counter-ordinal> TAB <row-ordinal> TAB <mask-ordinal> TAB <u64-value>`

All declared group/function/mask/skipped lengths must exactly match the number
of following records. Require contiguous ordinals, unique field keys, no absent
or extra fields, and exactly one verifier label per verifier group. Reject any
truncation, duplicate, reordered/unknown record, out-of-range numeric value or
extra stdout. Values must come from stdout; never replace a missing value with
zero, a fixture expectation or a capacity bound.

Current labels contain no tabs/newlines and are fixed by the authored probe:
`blocks=<B> reversed=<0|1> valid` or
`blocks=<B> reversed=<0|1> invalid-use`.
Producer ordinal0 is the4-loop input, ordinal1 the16-loop input. This describes
fixture identity, not observed counts. Atcap64 the driver is expected to return
four verifier records (32 valid/reversed and invalid-use/reversed), and one
skipped128 size. Those remain expectations until captured. The schema's census
category contains exactly one invocation report, not a claim of complete Census
emission. Its per-function `complete=0` and absent later rows must remain visible.

Record semantics:
- Parser_nodes is actual retained parser node count, not Expression entries.
- Predecessor_reads measures the dominance predecessor walk, not CFG building.
- Word_intersections counts each low/high word separately.
- Function parameters counts entry plus block parameters; signature_parameters
  separately names the source signature count. Arguments includes edge/call spans.
- Function rows cover only bodies reaching Statements. A prepass rejection or an
  earlier function failure can leave later rows absent. Foreigns have no row.
- Write masks are actual prepass masks, not inferred from emitted parameters.

Require raw process exit0 and both terminal markers. If probes print a report but
an old regression later fails, report-end may exist but regressions-passed must
not; this is a failed execution, not accepted counts. A successful marker is not
permission to skip independent result review. Preserve raw stderr separately;
never redirect it into stdout to manufacture this schema.
