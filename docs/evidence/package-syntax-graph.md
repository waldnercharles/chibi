# Package syntax to private graph: verified feature

Status: the original fresh build and four finite production results remain
retained. The post-review blank-package correction has passed its minimal
incremental build and owning 11-case/292-assertion route at qualified tree
`0851e0094f2ec24f620c0f25a21a609542cacf4a`. Publication adds only updated policy
workload comments and this evidence document to that tested tree. Executable
statements and product sources are unchanged. A fresh published-head review and
the separate dependent integration gates remain required.

## Post-review policy workload correction

The exact-9872 review correctly found stale workload counts in the production
policy comment after the blank-package regression. The comment now describes
11 cases, 31 canonical parses plus one edit, 32 extractions (18 accepted and
14 refused), and total submitted snapshot text below 8192 bytes. Nine graph
attempts, all other bounds, the selector and the 60-second deadline are unchanged.

The prior independent source/workload admission and observed 11-case/292-assertion
result already covered that actual workload. This correction changes only Python
comments and evidence prose; the production supervisor's executable statements,
test cases, C sources, generated parser and qualified ELF remain unchanged.
No formatter, generator, build or product test was rerun for this comment-only
successor. The earlier P1 finding and all prior outcomes remain retained; fresh
exact-head review and separate dependent integration remain required.

## Post-review blank-package correction

The exact-f368 review found that `package _` parsed and extracted successfully,
then violated the graph constructor's non-blank-name assertion. The earlier
10-case/287-assertion result was genuine but did not cover this input. The narrow
correction returns one located extraction diagnostic, `A package needs a non-blank
name`, before allocating any extraction owner or copying a name. The graph
invariant, grammar and generated parser remain unchanged. Longer underscore
names and explicit blank import aliases remain valid.

The new fixed pico_unit case uses a 46-byte UTF-8/CRLF specimen. It verifies an
error-free canonical parse, a NULL extraction owner, exact name range [18,19),
line 2/byte column 9, the `_` token and diagnostic text. No graph call is attempted.
The new NULL-owner assertion is source-proven to fail on the predecessor; a
separate old-code product run was not performed. Existing blank-import success
cases remain unchanged.

The three authored files passed formatting and dry run in 0.100 seconds. The
reviewed incremental build took 11.785 seconds, compiling only the two affected
translation units and relinking once, plus the unchanged VerifyGlobs check. Its
complete 2,461-byte log is retained (SHA256
31e184ed0d47bd407b8df676a778e72382b5ab19ecf81dc08189673854b134ac).
All 369 actual dependency files, all archives and every other build file remain
byte-identical; only the two objects, executable and Ninja deps/log changed.
The final ELF SHA256 is
63cf0633679b977987abf31fec43a4fc5b2dcdfa36f484e7871c5c6de24d6d13.
Independent inspection again found only frame_dummy initialization, standard
C-runtime finalization and the same libc/libm/system-loader closure.

The owning language-package-syntax route passed 11 cases, 292 assertions and one
suite under the unchanged 60-second policy: 0.072 seconds production time,
15.648 seconds recorded checked operation, terminal sample at 15.718 seconds.
Complete before/after input checks matched and no live descendants remained.
The runtime result SHA256 is
cac9213d8da0b9e080c024397597e710ddf9fd43a9adb1c3b740385763b56b90.
The other three routes were not rerun for this private extractor-only repair;
their original results below retain their original tree attribution.

## Boundary and ancestry

This slice follows package graph PR #431, commit
`ef303fe3c320b2aca5e6c08a996b862883f2632b`, tree
`4d2150c100408c37d2934f2a8a122aed827271b4`, whose sole parent is numeric
PR #430 head `b3bfbb828deae8737e1b122b4b3926bfd8eeeedb`.
The source donor's actual local HEAD/index is
`965a1c5e7f5ed4f908dbbf507323e524d6818fc6` with qualified overlays. It is not the
remote head. Its tested tree `3ab636ece1854115a06bb4edc87234ccfa1c5985` differs
from the graph publication tree only by that PR's evidence status document.

The canonical source_file grammar gets an additional package-header branch.
Legacy behavior/library/system/component roots retain their shapes. The words
package and import remain contextual so previously legal legacy names keep
parsing. A package header precedes its declarations; imports are top-level and
file-local. Package functions, constants, globals, records, unions, macros and
existing behavior/system/component declarations reuse existing grammar rules.
No nested package, alternate alias spelling, or library wrapper inside a package
is introduced. Visibility, enum syntax and new runtime behavior remain separate.

The extraction adapter reads script_parse/script_edit's immutable concrete tree.
It never scans tokens independently. It copies package names and explicit aliases
and emits the existing source_package_file/source_package_import descriptors.
Header ranges cover the name token. Import ranges cover import through the final
quote of its path, excluding terminators/trailing comments. Path ranges retain
the exact quoted token, including escapes; alias ranges include explicit _ and
are all zero for an implicit alias. Line/column follow script_range: one-based,
UTF-8 bytes, half-open byte spans.

## Ownership and resolution

The extraction object owns copied name/alias strings, descriptor arrays and the
parallel path/alias range array. File path and canonical member directory are
borrowed inputs. Import directories initially remain NULL. The caller must
supply every canonical resolved directory before graph construction, and retain
the final descriptor arrays plus all referenced strings until the graph dies.
The syntax is read only during extraction; retain it separately to interpret its
source ranges. An invalid canonical parse, a legacy root or the single blank package name `_`
returns one source diagnostic and no extraction object. No partial graph is published.

The fixture map matches each declaring file and its exact quoted import token to
an explicitly supplied canonical directory. It does not decode/normalize paths,
check real roots, read the filesystem, infer a directory from a header, or create
a package resolver. Future path resolution must handle language string decoding
and collection/root containment before constructing a real graph. Multiple
spellings can map to the same directory. Default aliases still come from that
directory's basename through the existing graph implementation.

The adapter is compiled only into the existing test_language dispatcher; it is
not exported by the SDK or called by the normal source catalog. Existing semantic
analysis continues diagnosing package_header/import_declaration as unsupported
program roots. Passing these parser cases does not establish package compilation, dedicated
package formatter, editor/navigation, runtime or migration coverage. No existing project
uses this additive private contract, so no project version/migration changes are
proposed.

## Finite owning-dispatcher workload

Owning selector: test_language --package-syntax. Eleven pico_unit cases contain:

- Four legacy root/name compatibility specimens and nine malformed/mixed header
  specimens, including a missing separator, late/duplicate header, quoted name,
  unsupported trailing alias syntax and imports inside a behavior body.
- Four files across three packages: alphabetical sibling membership, a separate
  nested directory, file-local alias reuse, canonical target identity from two
  path spellings, a default alias differing from the header, and repeated blank
  imports with an edge but no binding.
- A blank package-name refusal before owner allocation, with an exact located diagnostic.
- Header mismatch and duplicate alias failures preserving both source ranges.
- Named/blank self-import identity and dependency-first ordering, plus an unused
  genuine two-package cycle whose witness excludes the diagonal self edges.
- Exact UTF-8 byte offsets, CRLF line/columns, comments, explicit semicolons,
  implicit terminators and optional alias locations.
- A missing resolved target and an incremental header edit whose failed graph
  leaves the original snapshot and graph usable.

Static maximum for this fixed selector: 31 script_parse calls and one script_edit,
32 extraction attempts (18 accepted, 14 refused), nine graph constructor attempts
(four accepted, five refused). Four source snapshots or three during the edit
case coexist; at most four files, three packages and five imports feed a graph.
Every input is less than 256 bytes, so total submitted parse/edit snapshot text
is below 8192 bytes. Arrays cap at four files/twelve imports; map search is bounded
by four entries, with no recursion, clock loops or subprocesses. Package-local
owned descriptor/name storage has a proposed source-derived 16 KiB envelope,
not an allocation measurement. Whole supervised operation timing is reported below; this does not
claim an isolated Tree-sitter allocation measurement. No world, lowering, backend, GPU, network,
authored-code execution or filesystem fixture is part of this selector.

The predecessor --package-graph selector, canonical syntax regressions and
canonical formatter were validated separately against their reviewed finite
workloads. The sole new tools/test_policy.py row is language-package-syntax;
it changes neither the 60-second supervisor nor any aggregate. Build/configure
and generator timing are separate from each production test deadline.

## Retained source-tool evidence

Official Tree-sitter CLI 0.25.10 at commit
da6fe9beb4f7f67beb75914ca8e0d48ae48d6406 generated exactly six outputs with
`tree-sitter generate --abi 15 --js-runtime node` and Node v24.19.0.
The CLI binary SHA256 is
de21a99ed95e683526b26eee456e12352752487daed0d20afc85157212ab8e3d.
Generation plus both version probes completed in 0.547 seconds. All six raw
outputs were retained. Formatted alloc.h, array.h and parser.h equal their
baseline bytes; only parser.c, grammar.json and node-types.json change.

The four authored C/header inputs passed clang-format 22 and its dry run.
Tokens/directives were preserved except an equivalent adjacent usage-string
split. The initial generated-format attempt stopped at its 128MiB guard and is
retained as stopped. The separately reviewed 512MiB successor completed format
and dry run in 5.547 seconds, sampled peak 496209920 bytes. The complete
2158819-byte raw-to-formatted diff is retained (SHA256
85872746e8c3c32e8897f8ac58af2e0b4dc89ef724889e32651b945894c28659).
It includes 340 InsertBraces pairs; other tokens match. It is not whitespace-only.
The formatted parser is 1185542 bytes (SHA256
6cbceef2e0ee20c074c8b50401692f18ced3a60f7831fed195dd6dc2b6099a3b).

Canonical formatting walks all CST leaves, but these eleven cases do not provide
dedicated package-format round-trip coverage. A source-derived CR-only package
header separator edge is tracked separately as
[CUT-81](https://linear.app/cute-engine/issue/CUT-81/preserve-cr-only-package-header-separators-in-canonical-formatting)
and engine issue #432. It has not been executed here. Package/import stay
contextual rather than becoming global reserved words; the four legacy specimens
cover both names at old root/inner positions. No legacy keyword regression has
been demonstrated.

## Original qualified build and finite production routes

A distinct source was materialized and a fresh one-job native test_language build
completed in 83.637 seconds: 369 compilations, 55 archives, one link and the exact
VerifyGlobs check. No relocated objects or Ninja log qualified the parser. The
complete 52,823-byte compile log is retained (SHA256
5394d6db4ff29f29a7d06fceed71b2189f7fc712c199856b81e110f671431919), with no
compiler warnings/errors. Sampled inclusive build peak was 190,619,648 bytes;
these guards are sampled thresholds, not hard aggregate RSS limits.

All 369 dependency files and the actual link were audited. The 367 predecessor
dependency lists are unchanged after source-root normalization; only the two
intended translation units and their private header are added. The executable's
initializers contain only frame_dummy and standard C-runtime finalization;
dependencies are the system loader, libc and libm. No package/runtime constructor
or authored/backend execution is introduced.

These original results qualified tree `45e3f0f2020faa2be96e3214430c06e8d20f381d`
before the post-review correction above. Each route below ran separately through
the unchanged production 60-second
supervisor, including in-test fixture setup. The outer operation also checked
the complete source, executable, dependencies and protected inputs before/after.
No live descendant remained. Configuration/build time is not counted as test time.

From the engine root, run these four routes separately and serially against the
configured language build (do not reconstruct an unadmitted aggregate):

```sh
CHIBI_LANGUAGE_BUILD_DIR="<configured-language-build-directory>" python3 tools/test_policy.py tool language
CHIBI_LANGUAGE_BUILD_DIR="<configured-language-build-directory>" python3 tools/test_policy.py tool language-format
CHIBI_LANGUAGE_BUILD_DIR="<configured-language-build-directory>" python3 tools/test_policy.py tool language-package-graph
CHIBI_LANGUAGE_BUILD_DIR="<configured-language-build-directory>" python3 tools/test_policy.py tool language-package-syntax
```

| Route | Cases | Assertions | Suites | Production seconds | Complete checked operation seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| language | 12 | 463 | 0 | 0.067 | 15.449 |
| language-format | 40 | 3125 | 0 | 0.615 | 16.349 |
| language-package-graph | 23 | 457 | 1 | 0.056 | 15.451 |
| language-package-syntax | 10 | 287 | 1 | 0.057 | 17.108 |

The final default corpus has 89 checked-in files totaling 63,497 bytes, maximum
5,489 bytes. Source-derived workload counts are 110 canonical syntax parses
and two edits including that corpus; the formatter corpus alone contains 534
formatter calls and 890 parses across three modes. Assertions and timings in
the table are observed results. The fixed formatter cases retain their separately reviewed
semantic/minimizer work and 300-parenthesis rejection. No dedicated package
formatter coverage is inferred from those existing canonical cases.

### Retained corpus failure and narrow correction

The initial syntax run used the discovered 91 files/70,644 bytes and failed one
corpus case: 11 fixed cases passed, 471 assertions ran, and the two intentionally
historical Studio declaration inputs were rejected. A single frozen predecessor
comparison reproduced the same two rejected files, case totals and assertions.
OutlinerRows diagnostics match exactly; VirtualList shares its first four errors
but differs in later recovery positions. The strict comparison wrapper therefore
remains STOPPED, not an exact-diagnostic-match pass. This establishes inherited
rejection/corpus selection, not cross-version recovery equivalence.

Only OutlinerRows.chibi and VirtualList.chibi under test/fixtures/studio-declarations
are removed from built-in canonical corpus discovery, immediately after discovery
and before explicit external project/manifest inputs. Their bytes, sidecars and
dedicated project_declarations.c migration consumer remain unchanged. Those tests
migrate these legacy originals before parsing; they were not executed in this
focused slice. Explicit caller-supplied corpus inputs remain strict. This is not
a directory-wide exclusion or a grammar relaxation.

After the sole CMake edit, a bounded reconfigure updated metadata/corpus only.
A read-only graph capture proves all 425 compile/archive/link command bytes are
identical. All 1,076 retained product/input files remain byte-identical: 369 objects,
55 archives, the executable, 369 dependency files, two Ninja state files and 280
generated dependency inputs. No recompilation or relink was needed. The previously
passing package-syntax result remains applicable; affected syntax/format routes
and the previously unrun graph route passed on the corrected corpus. Both stopped
observations, original corpus and full build log remain retained.

### Operational incident and holds

A read-only-intended Git status inspection refreshed the frozen donor's index
stat cache. Object IDs, paths, modes and product source were unchanged. Execution
paused; the changed/original index bytes and incident were retained. A separately
reviewed one-file atomic restoration recovered the exact original index, followed
by complete frozen-inventory verification before later operations. This incident
does not relabel any stopped result as passed.

The initial PR #431 automatic-review usage failure is historical: a necessary
re-review completed clean on unchanged ef303fe3 at 15:00 UTC on 2026-10-17 and
received Codex's thumbs-up. The branch-retention setting was separately corrected
to retain branches at 14:42 UTC. Neither is a current blocker. This successor
still requires fresh exact-head review and separate dependent integration gates.
Full native/render/Studio, runtime package activation and other quarantined
coverage are disabled or unrun, not passed. Published-head correspondence remains
a separate required review.

## Explicitly outside this slice

Filesystem traversal, path/string resolution and root containment; declaration,
body/name/privacy/export resolution; normal catalog activation; formatter and
navigation support; authored enums; project migration; numeric frontend/default
changes; runtime package state, live publication, initialization or shutdown.
Approved future lifetime remains one shared package state per world, isolated
previews, once-at-start init and shutdown fini, compatible edits preserving state,
and initial restart for membership/incompatible-global changes. Task handles keep
caller-instance rights even when copied into package storage; tracing that storage
and ownerless init/fini task creation are separate future work. No task-storage
ban, queue-order bug compatibility or altered self-import identity is introduced.

