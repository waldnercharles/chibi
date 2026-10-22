# Selected runtime attribution: measured validation

Validated 15 October 2026. This records engine instrumentation, not Studio
application acceptance or completion of all B04/L14 work.

## Exact source and review

- Focused candidate: `3fccc552ab6e6714c50d607239d839dcc11a8991`, tree
  `d018af7a0a0064c65533cc2a6c5f7c2c04ba3c08`.
- Integrated source: `b2c9ae48685a38b0a6b0c0d3de187a9d9b67f555`, tree
  `cd887a20830f7815438bf6686a7c84078e94fcc9`, includes eligible main
  `c02cff784e141ae33effa841a7c5b93047f35104`. Held PR #407 is absent.
- The clean integration changed only the nine already-reviewed capture/roadmap
  documents. Product, fixture and supervisor bytes stayed identical to the
  focused candidate. The historical `implementation-sources.tsv` and
  `workload.json` remain source-freeze records; their old roadmap hash does not
  identify the later integrated documentation tree.
- Independent review approved the integrated source, actual builds/installed
  consumer boundary and complete finite workloads. Postflight verified all
  2,904 tracked files and 1,040 artifact records unchanged. Final report digest:
  `69d13a8567e4d177c8a422a150c103d30edcfeaeb2c7da90faffdff6a4ddd2b4`.
- Final documentation/log packaging is a later documentation-only delta.
  Its independent review and exact remote publication identity are recorded in
  the feature PR; this record does not invent a submitted GitHub approval.

## Fixed focused results

Each production-supervisor invocation ran serially, with no changed repetitions
or environment workload
knobs. Configuration and compilation were timed separately.

| Target | Cases | Assertions | Seconds | Exit |
| --- | ---: | ---: | ---: | ---: |
| Native Debug ASan/UBSan | 6 | 69,013 | 0.113 | 0 |
| Native Release | 6 | 69,013 | 0.056 | 0 |
| Serial Wasm / Node 24.19.0 | 6 | 69,000 | 0.216 | 0 |

All six fixed cases cover selected descriptor/payload copies, nested scopes,
interned-string gauges, task ownership windows, actual compiled backend entry
and paired scope-on/scope-off samples. The exact envelope and controls are in
`workload.json` and `implementation.md`. No extra mutation or no-op-hook binary
was introduced for these runs.

One enabled eight-cycle task-owner window reports 96 descriptor assignments,
4,608 text bytes and 40 actual execution entries. Existing wrapper copies are
256 bytes on native and 192 on Wasm, reflecting `sizeof(pin_value)` on each ABI.
These are fixture-specific operation counts, not a general speedup or global
copy census. Raw paired samples remain in the focused logs. Scope-off samples
retain dormant compiled hooks; they do not measure the cost of having hook code
at all. No overhead percentage or application performance win is claimed.

## Complete affected-contract closure

Independent review used the exact fixed C workloads, 89 checked-in corpus
files totaling 63,497 bytes, existing bounds and previously measured complete
command capacity. Each new route occurs once in its ordinary group. No prior
member was removed; the unrelated 66-member native `test_tools` selector group
was not expanded or reconstructed.

| Complete command | Members | Cases | Assertions | Seconds | Exit |
| --- | ---: | ---: | ---: | ---: | ---: |
| `group language` | 31 | 409 | 108,359 | 20.430 | 0 |
| `group wasm` | 19 | 253 | 104,425 | 3.985 | 0 |
| `tool native-tools` | 1 dispatcher | 116 | 33,227 | 23.856 | 0 |
| `tool language-prepared` | 1 workflow | 19 | 833 | 0.234 | 0 |

The prepared workflow totals six producer cases/199 assertions, twelve separate
runtime-only consumer cases/629 assertions and one parent case/five assertions.
All four complete commands ran through the production supervisor.
No unexpected failure, sanitizer finding, crash or timeout occurred.
Expected failure messages inside tooling policy/package tests are their passing
negative controls, not failed top-level invocations.

The language/Wasm groups exercise managed copies, tasks, backend/inspection,
publication/module lifetimes, borrow/activation, checked writes and prepared
reconstruction alongside attribution. Complete tooling includes public
array/union/record snapshots, ECS intern/dedup/managed-row lifetime, packaging,
configuration, production-supervisor controls and the existing generated-C case.

## Build and installed-consumer boundaries

Native correctness uses Clang 19.1.7 Debug, assertions and ASan/UBSan, with
LeakSanitizer disabled for the existing tracing limitation. Release retains
runtime `-O3 -DNDEBUG` and fixture `-UNDEBUG`; its timings are raw observations,
not sanitizer correctness evidence. Serial Wasm uses Emsdk 6.0.10 and explicitly
selected Node 24.19.0; this is generated-Wasm execution, not browser or pthreads.

Fresh compiler-enabled minimal tooling was built with native/desktop disabled
and the existing pinned local SLJIT/Tree-sitter dependencies. The generated-C
case is compiled in. A separate compiler/native/desktop-off SDK was installed
and used to build the existing consumer dispatcher against public installed
headers and libraries only. Its prepared child has no language/compiler,
authoring, graph or syntax library in its link closure. Public attribution
headers match source; private copy/runner/artifact/definition headers are absent
from installation. An earlier missing local SLJIT configure path was corrected;
its failure is not represented as a passing build or runtime result.

## Evidence and limits

`validation.json` lists exact commands, results and every published log digest.
The seven logs preserve all test cases and their outcomes. Source-workspace
prefixes, test-owned temporary-root prefixes and the machine-specific invocation
PATH use `$SOURCE`, `$TMP` and `$NODE24_BIN:$BASE_PATH` where present. Retired
test-policy banners and ceiling metadata are omitted; nonexecution and rejected
invocations state their recorded outcomes. Test names, counts and timings are
unchanged. Each record retains the original and prior published SHA-256 alongside
the current published digest, byte size and normalization details. The pinned
Node version and artifact identity remain in the build evidence.

The historical `workload.json` also replaces machine-specific source and toolchain
roots with `$SOURCE` and `$TOOLS`. `validation.json` records its original and
published hashes and normalization details. Retired test-policy fields are
omitted. Dependency revisions, command flags, binary hashes and the original
proposed execution status are retained. Map these path
placeholders to local directories before reproducing the historical build.

This covers selected copies, actual backend entries and sampled retained intern
storage. It is not a global allocation/copy census, semantic live-byte count,
unsampled peak, leak proof or whole-application performance result. The optional
no-op-hook control, unrelated full native/material/project/render/Studio suites,
browser and pthread Wasm remain unrun. Original Studio SDKs separately compiled
natively and for serial web; this feature does not prove their runtime behavior
or compatibility with a newly released engine SDK. Missing CI is not passing CI.
