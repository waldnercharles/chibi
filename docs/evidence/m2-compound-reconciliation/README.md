# Compound-assignment and boxed-parameter reconciliation

Local source checkpoint `1ea4cd098533aa9f84df3cbf3f0c80097a85037b`, tree
`23dbf07ebf21172c10f31d3ffca044020933464a`, on
`codex/m2-compound-reconciliation-20261025`. Parent is the retained, fully
validated S candidate `072d9ee157aabea7f7c60f6a7e5a92993639f8a5`.
The normal reconciliation merges frozen author head
`cf24c7b66af64dbb6b46df041308f49ba4b0c6bf`; no author branch is rewritten.

## Source and contract

Only Steps.chibi, References.chibi and the existing C/pico compiler fixture
change from S. The eight arithmetic/bitwise compound operators require equal
scalar integer types before widening or reference/box/field delegation.
Plain assignment, floating conversion, contextual literals and differing-width
unsigned binary shift counts retain their separate contracts. RHS typing and
evaluation still happen once. No unsupported compound-shift syntax is added.

The new positive boxed-parameter fixture exposed an inherited extra target
watch. References now omits the second stored-value watch only when the binding
is a parameter; local watches, initial read watches, reference and field paths,
and actual compute/write instructions remain. Two new positive samples require
existing full source-point/watch comparison on both compiler backends, without
an ahead exemption or fabricated host effects.

## Executed evidence

- Red: seven exact diagnostic cases were wrongly accepted on both authored
  compiler backends, giving 14 intended failures. Independent stage-zero
  diagnostics matched all seven literal messages/positions. Red scratch adds
  only case selection; every assertion remains intact.
- Intermediate green: the two-file guard passed 13/14 selectors; the inherited
  boxed-parameter watch mismatch remained. The earlier S implementation also
  reproduced it. These failed receipts are preserved, not overwritten.
- Final three-file source: **14 selectors, 99 cases, 82,362 assertions passed**.
  C fixture compilation/linking both succeeded. The tests execute the authored
  Chibi compiler on both bytecode and SLJIT and execute emitted calls on both
  runtime VMs, with independent literal results and stage-zero diagnostics.
- Negative control: revert only `&& !Parameter(forest, binding)` in scratch
  References. Both collections selectors fail: watch505..506 appears twice
  instead of once. All C fixture bytes/assertions and Steps bytes match the
  passing run. Control compile/link succeed. The production source is unchanged.
- [Independent review](final-three-file-review.md) covered the final exact
  three-file implementation at `1ea4cd098533aa9f84df3cbf3f0c80097a85037b`
  and verified receipt/log hashes, both control failures, and unchanged assertions.

Final selector list:

- `language-package-compiler-chibi`
- `language-package-compiler-chibi-compiled`
- `language-package-compiler-chibi-collections`
- `language-package-compiler-chibi-collections-compiled`
- `language-package-compiler-chibi-types`
- `language-package-compiler-chibi-types-compiled`
- `language-package-compiler-chibi-debug-free`
- `language-package-compiler-chibi-imports`
- `language-package-compiler-chibi-imports-compiled`
- `language-package-compiler-chibi-native-arguments`
- `language-integer-shift-counts`
- `language-integer-shift-counts-authored`
- `language-semantic`
- `language-integer-semantics`

These are source/fixture tests, not a new default-assets, bootstrap or native
LLVM acceptance claim. Only the current C fixture object was rebuilt against
exact9711 native objects. Its scratch embedded-source header replaces exactly
Steps and References, verifies old arrays against baseline source, and records
production/embedded hashes. Every other native source/build input matches that
verified reference. `run-final.py` and exact commands/flags/log hashes in
`fixed/results.json` document the procedure. The normal supervisor supplies
finite120s build/300s selector limits; no warning/sanitizer suppression or
capacity changes. No new executable download or bootstrap generation.

The inherited-watch reproduction log is valid; its shell wrapper subsequently
failed assigning zsh's read-only `status` variable. The raw failed test and that
wrapper distinction remain in `watch-blocker.json`. `interim.json`, the older
proposal inventory and green receipts describe earlier states, not the final
passing source. Scratch executables remain under `.chibi/compound-proof` for
this still-pending integration review; none is included in this checkpoint.

## Normal merge resolutions

`actual-conflicts.json` retains every conflicted text hunk. The resolution
script and `per-hunk-resolutions.json` record individual selections and explicit
automatic-hunk removals. This was an ordinary merge, not an ours-strategy merge.
The genuinely missing author compound guards and two diagnostics were adapted
and proved in the preceding source commit, expanded to seven paths and positive
controls. All non-documentation blobs after reconciliation equal that tested
source checkpoint; `checkpoint.json` records the count and canonical inventory hash (reproducible
from the source commit), including symlink target identities. No source changes are hidden by resolving only conflict markers.

| Files | Resolution |
| --- | --- |
| Steps, References, package_compiler_chibi.c | Keep tested narrow compound/parameter repair, no-ahead full oracles, conditional literal watches and ~0 regression. Author duplicate compound cases are included in the seven-case adaptation. Two author watched_once spans are already checked by the full multiplicity oracle; do not import their ahead bypass. |
| Body | Parenthesis guard already present; no behavior change. |
| Compile, Number | Preserve ordinary FoldConstant owner and reviewed typed-floating fixes. Remove auto-added older FoldFloat and its competing conversion helpers; retain nested signs/grouping, untyped contextual overflow and integer-bit guards. |
| float_conversion_cases.h, semantic.c | Retain exact narrowing rejection diagnostics and independent positive rounding/overflow/subnormal literals; no contradictory permissive policy. |
| catalog.c, type_relations/Relations.chibi, type_relations_chibi.c | Automatic result already retains strict explicit narrowing; verified against tested source. |
| compiler_artifact_chibi.c | Keep real production snapshot bounds and full retained-source checks. Remove the auto-added arbitrary SOURCE_CAP alternative; preserve existing other bounds. |
| compiler_host.c | Resolve duplicate relocation to one existing public accessor, preserving main690. |
| main.c, package_compiler_chibi.h, packed.c, test_policy.py | Preserve tested dispatch, ownership, float controls and durable authored-shift gate; keep main957 and stage-zero repair. |
| bounded_compiler/Expression.chibi | Both histories already have explicit int64(digit); unchanged. |
| bootstrap pair | Retain exact S inputs b1d902d3/4256f99d, not author's31d5c893. Do not relabel source provenance. |
| language-friction.md | Keep author formatter-limit observation, verified against actual FMT_TEXT_MAX_ARGS and empty-output path; omit unrelated external-language comparisons. No formatter policy change. |
| native-llvm-ledger.md | Keep approved explicit narrowing and record implementation. Label author's source-overlay seed as historical, distinguish S's verified seed, and state new generation is pending. |

## Pending gates and unchanged inputs

S,9711 and historical8d evidence remain separate. The exact checked seed is still
13,673,789 bytes SHA256b1d902d35c4468e6de7844e9d29e4e0010dfeac2964c6e84926fcea98a6e5c71;
manifest24,311 bytes SHA2564256f99d40767bc50b20fd1676905fda0dd68e485ff6b0c4f6ca41c2cd3a363e.
The runtime pin is unchanged. The new compiler source inventory differs in
Steps and References, so the old pair is a historical bootstrap input, not a
current-source artifact. A separately reviewed current-source generation,
self-reproduction, production manifest, default asset gate, normal bootstrap
suite and exact-head native/independent platform checks remain required before
publication. No PR688/693, release, pin update, force push or public seed push.

Raw logs and unified patches retain their original trailing spaces, including
patch context markers; they are evidence bytes, not reformatted source.

Independent composition review accepted staged tree
`80ecce4635d40d7de6dd63b115b4eca42aea0c68`: all2,467 non-documentation entries
match the tested source, all57 conflict hunk hashes agree, frozen cf24 is the
merge parent, and seed/pin identities are intact. Subsequent changes only add
this review receipt, the precise two stale manifest records and attribution.
The Linux totals in the [ledger](../../native-llvm-ledger.md) were independently
reported for verified candidate `9711a2f36686e52765b7dd6d28c659b100867971`, tree
`3faa7706f4d78123c3ed99982b9ba654c863124f`; they are not a Linux run by this
Mac lane or by this composition reviewer. Mac and Linux history remains separate from
current-source acceptance. The archived scripts preserve the executed scratch
procedures and expect their recorded baseline/source views and fresh output
directories; they are not a new public build interface.
