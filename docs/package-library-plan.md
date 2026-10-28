# Odin-inspired package library

Build Chibi's library through explicit package decisions and dependency-ordered
implementation. The owner approved this program on 2026-10-20, chose fidelity
separately for each package, and asked that useful language features be
implemented before the ports that need them.

## Current checkpoint

- Planning base: `4a751bd5fb2bb414ec867f089acae9b9736e1d9e` in `chibi`.
- Branch: `codex/odin-package-foundations-20261020`.
- Worktree: `/Users/cwaldner/Personal/chibi/.worktrees/odin-package-foundations/chibi`.
- Integration base on 2026-10-21: `efaafdcf4cdc31e6423ae20d49c97418ee375105`
  from fetched `origin/main`; includes the array-method/parameter-copy fixes,
  third-party archive build changes, M6 native type libraries and the graph's
  tree-sitter/physics library targets.
- Upstream comparison: Odin `779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`.
- Program and per-package decision process: approved.
- String invariant approved on 2026-10-21: immutable, length-carrying,
  byte-preserving strings, including NUL, with explicit UTF-8 operations.
- C-string safeguards approved on 2026-10-21: diagnose provably invalid uses
  during compilation and validate unknown values before native effects. New
  public APIs accepting external data expose recoverable errors. The descriptor
  migration and regressions pass the selected native, language, Wasm, material,
  project and tooling checks. Bootstrap publication is recorded below.
- New text/binary APIs and new public collection names: proposed, awaiting
  semantic review. Approval of the program does not settle these choices.
- Managed pointers replace `ref`: a `^T` parameter that does not escape
  lends the caller's location, `F(&value)`. The [pointer contract and
  verification ledger](managed-pointers.md) record supported operations and
  remaining limits.
  The separate [sequence workstream](value-sequences.md) owns value arrays/maps
  with copy-on-write storage, copied ranges and explicit pointer-backed views.
- Shift behavior approved on 2026-10-21: reject counts outside the operand
  width and discard shifted-out bits. Implemented and verified locally across
  compiler stages and execution backends in this branch; not merged into main.
- First implementation: stage-one package visibility, implemented locally,
  reviewed and tested in this branch; not merged into main.
  Private-by-default declarations and explicit `pub` are already approved and
  implemented by stage zero. This increment applies that contract to stage one.

## Sequence and completion criteria

| Step | Deliverable | Completion criterion | Status |
| --- | --- | --- | --- |
| P0 | [Odin inventory](odin-package-inventory.md) | Every source package directory in the pinned base/core/vendor trees is accounted for, including internal/test directories; current Chibi coverage and dependency groups are explicit. | Complete: 355 source directories mapped; individual feature reviews remain |
| P1 | [Foundation decisions](package-foundations.md) and [numeric contract](package-numeric-contract.md) | Each first-wave feature has a proposed API/example, alternatives, compatibility consequences, prerequisite list and an owner decision. | Proposals ready; semantic decisions pending |
| P2 | Language/runtime prerequisites | Approved prerequisite behavior works through its owning compiler/runtime paths, with meaningful regressions and declared target coverage. | Visibility, shifts and string descriptors implemented locally; shared slices and remaining checked arithmetic pending |
| P3 | First portable package batch | Each approved subset imports, compiles, runs, documents and installs through the SDK; fidelity gaps and friction are recorded. | Waiting on P1/P2 |
| P4 | Further package batches | Repeat P1-P3 for data structures, data formats, host services and selected vendor bindings. | Queued |

The current integration preserves the existing work in the isolated worktree.
Discuss each subsequent step's semantics before its
implementation, and reuse completed work from other branches. Settle the
remaining foundation choices before dependent packages. Reviewing
a package family does not approve every declaration in it. A decision can
approve a coherent subset and defer the rest.

## Decisions and ownership

The [toolchain plan](../PLAN.md#language-and-package-ownership) owns the existing
`core:`, `game:` and `vendor:` boundaries. ECS stays a language contract.
The foundations proposal considers how Odin's `base:` responsibilities fit
those boundaries; creating a public `base:` is a separate decision.

Package implementations live in `packages/<collection>/<package>/*.chibi`.
Compiler and runtime work stays in its owning subsystem. External libraries
remain behind explicit native bindings. A package import must not silently
acquire filesystem, process, network or engine access; target capabilities and
unsupported-target diagnostics belong in its contract.

The owning source declaration is authoritative for the implemented API.
Decision records explain intentional semantics and differences from upstream.
The inventory records coverage, this plan records sequence, and
[language friction](language-friction.md) records cross-package limitations.
Keep each fact in its owning document and link to it from the others.

## Prerequisites before ports

Prioritize features that make the selected package correct, keep its API usable,
or prevent a measured cost from dominating its intended use. Include a concrete
consumer and acceptance example for every prerequisite. Broader convenience
features can wait for another package to demonstrate the need.

| Prerequisite | First consumer | Decision state | Sequence |
| --- | --- | --- | --- |
| Package privacy in both compiler stages | Every public package and its internal helpers | Existing `pub` contract | First implementation |
| Length-carrying strings, embedded NUL handling and byte conversions | Strings, UTF-8, codecs and compiler text | String invariant approved; API signatures remain proposed in foundations | Before expanding text APIs |
| Checked arithmetic with explicit wrapping | Hashes, checksums, sizes and random generators | Default behavior already approved; edge cases documented in numeric proposal | Before new overflow-dependent algorithms |
| Record fields that compose value types | Builders and containers holding byte arrays or records | Needs a separate concrete design, including copies and prepared layouts | Before those stateful APIs |
| Exported generic specialization | Slice algorithms and generic containers | Approved 2026-10-27; local function implementation under validation, with definition-site visibility and canonical identities | First stage before generic package APIs |
| Non-capturing procedure values | Comparator/predicate APIs | Approved 2026-10-27; local synchronous, non-escaping named/inline argument implementation under validation | Second stage after exported generic functions |
| Host capabilities exposed as imported packages | Shared I/O and build-library consumers | Needs a host/package design | Before OS/process/stream APIs |
| Foreign layout and lifetime contracts | Public vendor bindings | Per-library decision | Before a binding is assigned |

Stage-zero acceptance and stage-one acceptance are separate claims. Libraries
used by the compiler need both; an engine-only package must name any different
requirement. Existing language features that stage one lacks are compiler parity
work, not new language designs.

## Package decision record

For each candidate, complete these fields before implementation:

1. Use cases and short Chibi programs showing the public operations.
2. Pinned upstream declarations included, adapted or deferred.
3. Relevant alternatives from primary language/library sources.
4. Types, ownership, mutation, allocation, errors, complexity and edge behavior.
5. Dependencies, required language features and supported hosts/backends.
6. Acceptance cases, including deliberate differences from Odin.
7. Owner decision, implementation owner, dependency commits and current status.

Use proposed, approved, implemented, merged and verified precisely. A past test
result belongs to its tested commit and target. A mapped source directory is
not a completed package, and a checked function is not full upstream parity.

## Parallel implementation

After a batch's contracts are approved, give each agent a package or prerequisite
with exclusive file ownership, dependency commits and acceptance criteria. One
integration owner coordinates shared compiler/runtime files, SDK installation,
test dispatch and documentation navigation. Foundation changes land before
packages depending on them; unrelated pure packages can proceed independently.

Agents report new friction with a small reproduction, workaround, affected
packages, measured cost where relevant, prior-art links and a suggested change.
Extend a matching ledger entry rather than creating another. New semantic
choices return to the owner; already-approved behavior and routine fixes proceed.

## Branch coordination

The initial audit covered all 24 registered engine worktrees. The dirty primary
checkout and existing terrain/authoring work remain independent. Recheck live
main, branch ancestry and dirty state before every implementation batch.

- `cwaldner/first-class-vectors` at `be5ab05b9` contains unmerged vector/matrix
  work. Compare it before designing math APIs.
- `.claude/worktrees/trusting-davinci-8d48d9` contains an uncommitted
  `src/execution/module_effects.c` reference-sharing change; preserve it and
  inspect it before changing argument-sharing rules.
- `.claude/worktrees/upbeat-spence-90ef8b` was clean on `cwaldner/array-rename`
  at `72cfa7903` when inspected on 2026-10-21, equal to remote main. Its merged
  [string design](design-notes.md#strings-carry-their-length) and bounded byte
  scans are existing work. `src/types/value.h` still stores `const char *str`,
  `src/runtime/text_format.c` still copies slices, and string literals and
  wire decoding still reject embedded NUL. General length-carrying descriptors,
  accessors and shared slices are not implemented at that revision. Recheck
  this worktree before assigning string work; its active owner may advance it.
- Direct coordination with that Claude session confirmed ownership: this branch
  handles descriptors and C-string boundaries; Claude handles the array rename,
  native arrays and move-at-last-use work. Claude agreed to leave `record.c` and
  `array.c` unchanged until the descriptor checkpoint reaches main. Keep
  `packages/core/strings/Strings.chibi` out of this increment until its array
  rename lands. A pushed checkpoint is not a main-branch integration.
- [PR 569](https://github.com/waldnercharles/chibi/pull/569) and
  [PR 629](https://github.com/waldnercharles/chibi/pull/629) are merged in the
  integration base and supply third-party archives/build support.
  [PR 571](https://github.com/waldnercharles/chibi/pull/571) remains the SDK
  distribution draft at this audit. These do not establish public `vendor:`
  language bindings. Coordinate acquisition/install changes with them.
- [PR 600](https://github.com/waldnercharles/chibi/pull/600) is merged in the
  planning base. Measure performance against that implementation.
- `cwaldner/m5-package-imports` at `8cb2911ec` is an older unmerged alternative.
  Compare its behavior with the present resolver before considering any reuse.

Preserve existing holds and review requirements. Study overlapping branches;
import them only when their actual changes are needed and eligible.

## Verification

Follow the [testing instructions](testing.md) and report build/configure time
separately. Use the owning C/pico_unit dispatchers:
`test/language/package_core.c`, `package_imports.c`, `packages.c` and
`package_compiler_chibi.c`, plus runtime tests where representation changes.

For each selected package or prerequisite:

- Establish a failing-before regression for a defect, and exercise public
  behavior rather than reproducing implementation details.
- Compare stage zero and stage one where the package must support both.
- Run relevant bytecode/compiled-backend cases and the Wasm checks
  affected by the contract. Record unavailable required coverage as a gap.
- Verify installation and an external import for a newly shipped package.
- Measure representative text/container workloads when complexity or allocation
  is part of the contract; correctness tests alone do not establish performance.
- Inspect the combined diff and generated documentation before publication.

Library growth increases compiler-test execution time. Select bounded,
representative acceptance workloads and retain full-package import coverage
through the owning checks. Unrun suites and deferred host bootstrap runs
remain unverified. This program does not change the toolchain's host acceptance
requirements.

### First prerequisite evidence, 2026-10-20

The stage-one visibility change rejects imported private functions, constants,
record/union names and union constructors. Tests also cover nested type uses,
public wrappers around private helpers, same-package files and local names
shadowing an import alias. They compare acceptance and diagnostic locations
with stage zero. Private union constructors deliberately receive a clearer
stage-one private-name message; stage zero currently reports an unknown
callable/value. Private functions remain in prepared modules when needed by
their public callers; source privacy does not create a host security boundary.

The regression failed before the fix because stage one accepted `lib.Hidden()`.
The final current-source checks passed under the production test supervisor:

| Check | Assertions | Execution time |
| --- | ---: | ---: |
| Imports, bytecode compiler | 2,891 | 4.815 s |
| Imports, compiled compiler | 2,815 | 3.448 s |
| Argument sharing | 11 | 2.962 s |
| Current-source compiler bootstrap and sample parity | 9,759 | 21.656 s |

Run from the worktree root:

```sh
python3 tools/test_policy.py tool language-package-compiler-chibi-imports
python3 tools/test_policy.py tool language-package-compiler-chibi-imports-compiled
CHIBI_LANGUAGE_BUILD_DIR=build-release/test/language python3 tools/test_policy.py tool language-package-compiler-chibi-sharing
python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap
```

Independent review found no remaining actionable issues. C formatting and
`git diff --check` passed. These results apply to the local changes on the
planning base, not a published commit. The historical recovery check exports
`HEAD`, so it would not exercise these uncommitted edits; modified-source
recovery through the checked-in bootstrap artifact remains unrun. Broader
native/material/project/tooling suites and Wasm checks were not run in this
increment.

Task evidence remains under the ignored `.chibi/odin-package-foundations/`
directory: the failing-before output and verifier-budget failure. The latter
is recorded with its workaround in [language friction](language-friction.md).

### Shift prerequisite evidence, 2026-10-21

Invalid counts now fail in the VM, SLJIT, generated C and emitted Wasm. The
native and authored typed scalar folders agree; stage-one package constants
discard shifted-out bits instead of rejecting them as arithmetic overflow.
The resolved operand type determines width before any result conversion.
Signed right shifts retain sign extension.

The first runtime and scalar regressions failed because invalid counts were
masked. The package-constant regression failed because shifting into the sign
bit was rejected. Review found a further optimizer defect: an unused shift
could be removed and earlier stores delayed past a failure. Its regression
failed before the optimizer was restricted to proven-valid constant counts.

These focused checks passed on the local candidate under the production
supervisor:

| Check | Assertions | Execution time |
| --- | ---: | ---: |
| Native execution, VM and SLJIT | 3,685 | 0.597 s |
| Wasm execution, interpreter and emitted code under Node | 3,681 | 0.176 s |
| Generated C and VM parity | 8,256 | 2.046 s |
| Native/authored scalar constants | 19,580 | 0.271 s |
| Package types, bytecode compiler | 2,148 | 3.554 s |
| Package types, compiled compiler | 1,864 | 3.390 s |
| Authored prepared writer | 1,026 | 1.645 s |
| Scalar lowering | 33,382 | 0.175 s |
| Bounded compiler | 36,589 | 0.606 s |
| Prepared records and previous-format rejection | 1,036 | 0.115 s |
| Compiler self-compilation and sample parity | 9,909 | 17.724 s |

Native Release and Wasm Release builds passed separately. The Wasm build used
`EMSDK_PYTHON=/opt/homebrew/bin/python3.12`. Independent review found no remaining
correctness or ownership issues after the optimizer fix. Changed C files pass
clang-format 22 and the whole diff passes whitespace checks.

Prepared format 19 rejects older masked-shift programs. The authored writer,
scalar-lowering guards and bounded compiler use the same version. The bootstrap
contract is version 4, minimum 2. Its regenerated closure is 11,383,423 bytes,
SHA-256 `2f0a3d0007e528a43d735287998738566fa6941efe373b389db484ad195d3d54`.
Current-source recovery reproduced those exact bytes in 6.313 s. Generation
through the documented `bootstrap-artifact` target, including its incremental
build and self-checks, took 29.089 s.

At this pre-integration checkpoint, the artifact files were modified locally
with revision `4a751bd5fb2bb414ec867f089acae9b9736e1d9e-dirty`; no commit, push
or merge had occurred. The integration evidence below supersedes that status. The historical
HEAD-export recovery selector, Windows, browser-host execution and
full suites remain unrun. String storage, default checked arithmetic and lexical
wrapping are still pending; this checkpoint does not claim those prerequisites.

Generated scratch artifacts were removed after copying the verified pair into
`tools/compiler/bootstrap`. Task failure/build logs remain in
`.chibi/odin-package-foundations`; regular native/Wasm build directories remain.
No scratch executables or project copies remain; normal `build-release/`
outputs and `.chibi/logs/` build logs are retained.

### Main integration evidence, 2026-10-21

Checkpoint `9de30764f` preserved the existing implementation. Merge commit
`11bd35f7a4e275a09e810cfb4c19ec715695ef5f` integrates remote main at
`72cfa7903e54294a196e18ccd563b77087477657` in the isolated worktree. No other
checkout was edited. Independent review covered both the original changes and
their interaction with the merged array-method, mutation and parameter-copy
logic. The compiler-test conflict retains all three probe flags: `moves`,
`aliases` and `same_rejection`.

Native Release and Wasm builds passed. The following production-supervised
checks passed on the clean integrated source. These are selected affected-contract checks, not a full-suite
pass.

| Check | Assertions | Wall time |
| --- | ---: | ---: |
| `language-execution` | 3,685 | 0.398 s |
| `language-constant-scalar-chibi` | 19,580 | 0.455 s |
| `c-export-alias` | 8,256 | 3.174 s |
| `language-package-compiler-chibi-types` | 2,148 | 3.936 s |
| `language-package-compiler-chibi-types-compiled` | 1,864 | 3.273 s |
| `language-package-compiler-chibi-imports` | 3,226 | 6.130 s |
| `language-package-compiler-chibi-imports-compiled` | 3,084 | 4.328 s |
| `language-package-compiler-chibi-sharing` | 11 | 4.686 s |
| `language-prepared-writer-chibi` | 1,026 | 2.169 s |
| `language-scalar-lowering-chibi` | 33,382 | 0.216 s |
| `language-bounded-compiler-chibi` | 36,589 | 0.835 s |
| `language-record-prepared` | 1,036 | 0.118 s |
| `wasm-execution` | 3,681 | 0.263 s |
| Clean-source bootstrap generation, including self-compilation | 10,109 | 35.574 s |
| Recovery through that generated artifact | Exact closure match | 6.489 s |

The clean bootstrap records revision `11bd35f7a4e275a09e810cfb4c19ec715695ef5f`,
all 18 source hashes, 11,262,184 closure bytes and SHA-256
`c40689a0a5c3572f1d2d2cc24c077328748013205709cec3230867eff767cd21`.
It is byte-identical to the closure generated during integration; only manifest
revision metadata changes in the final checkpoint. Recovery reproduced those
bytes. Formatting with clang-format 22 and whitespace checks passed. Test logs
remain under `.chibi/odin-package-foundations/integrated-*.log`; generated
scratch closures were removed after verification. Windows, browser hosts,
sanitizer configurations and broader suites were not run for this
integration.

### String descriptor evidence, 2026-10-21

The descriptor cutover preserves arbitrary bytes, including embedded NUL, across
owned values, execution backends, records, arrays, persistence and length-aware
host APIs. C-only boundaries reject embedded NUL before native effects. Static
checks catch supported literal/constant proofs; runtime checks cover unknown
values. This is not whole-program proof. Existing no-result APIs retain
controlled invocation failure; new fallible public APIs remain a semantic review.

Prepared format 20 distinguishes authored descriptor fields from native C-string
fields. Bootstrap version 5 accompanies it. JSON uses strings for valid UTF-8
and a byte-array encoding for other string values. The prototype registry stays
authoritative for its own nested native types through call-scoped resolution;
the global type catalog is not changed by a prototype write. Failed native
conversions leave destination fields unchanged.

Merge `cb0faa79213ed2959d3f0d4a812eb444cb61a836` integrates main at
`e20bd6af83b1dd4e3fa48dcd5d1c4769bb818a97`. Native Release and Wasm builds pass.
The production-supervised `native`, `language`, `wasm`, `materials` and `projects`
groups and the tooling `short` aggregate pass. Focused checks cover record values/documents/prepared decoding, native
write atomicity, ECS ownership and observers, core strings, generated C,
compiler constants/binding/signatures/union members, prepared writing and
compiler self-compilation. These are selected checks, not a full-suite pass.

M6 build-graph, reflection and archive checks pass, including parity for 251
compile commands across 69 native libraries. The graph check used the same Xcode
SDK as the configured CMake build through `SDKROOT`; the shell's default
CommandLineTools SDK otherwise makes that comparison differ. Independent review
covered ownership, native conversion failure, nominal type identity, compiler
metadata and wire allocation budgets. Formatting uses clang-format 22.

Bootstrap generation and clean-candidate evidence are recorded in the following
publication checkpoint. Logs remain under `.chibi/odin-package-foundations/`.
Shared slices follow this checkpoint. Discuss retention behavior, arithmetic
edge cases and each package's API before assigning further work. Windows,
browser hosts, sanitizer configurations and broader suites remain unverified
for this increment.

### String bootstrap checkpoint, 2026-10-21

Source commit `796fd5b24038d9dd0e74ecd9eb07fa0ddfb178db` is the clean descriptor
source revision. Bootstrap version 5 records that revision and all 18 source
hashes. Its 11,362,875-byte closure has SHA-256
`4898f9dfa88a51a45383d0fafa67bfbd8e15b4a7ccc42e972377d173b409182e`.

Self-compilation, production-target parity and sample parity passed 16,038
assertions in 19.143 seconds under the production supervisor. The complete
seed-to-artifact generation took 25.198 seconds.
Recovery from a clean export of the recorded source reproduced the closure
in 7.023 seconds. The bootstrap target
contains production natives only; the C-string probe is explicit in its three
boundary samples and cannot enter the production seed silently.

Native and Wasm compiler sample checks pass after that target separation. The
remaining publication checks use the exact clean source/artifact combination,
with configuration/build time separate from test execution. Logs
remain in `.chibi/odin-package-foundations/string-*.log` and the group logs in
the same directory. Generated seed closures and migration scripts are removed;
ordinary native/Wasm build outputs remain. This branch is a review checkpoint;
main integration and subsequent package semantics remain separate work.

Main advanced during publication with the M6 syntax/physics build graph. Merge
`fab81617931854de4041e7621c8c6069b1cd1a0f` integrates
`efaafdcf4cdc31e6423ae20d49c97418ee375105`; its changes are graph recipes and
documentation. Compiler source hashes and the bootstrap closure above remain
unchanged. The combined branch retains `src/types/text.c` in the native type
library and uses a fresh clean-candidate validation before updating the draft.
