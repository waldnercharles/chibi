# Odin-inspired package library

Build Chibi's library through explicit package decisions and dependency-ordered
implementation. The owner approved this program on 2026-10-20, chose fidelity
separately for each package, and asked that useful language features be
implemented before the ports that need them.

## Current checkpoint

- Planning base: `4a751bd5fb2bb414ec867f089acae9b9736e1d9e` in `chibi`.
- Branch: `codex/odin-package-foundations-20261020`.
- Worktree: `/Users/cwaldner/Personal/chibi/.worktrees/odin-package-foundations/chibi`.
- Upstream comparison: Odin `779c8c0a4778f994389e2a681ffc7b3cbd6c1f65`.
- Program and per-package decision process: approved.
- String invariant approved on 2026-10-21: immutable, length-carrying,
  byte-preserving strings, including NUL, with explicit UTF-8 operations.
- New text/binary APIs and new public collection names: proposed, awaiting
  semantic review. Approval of the program does not settle these choices.
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
| P2 | Language/runtime prerequisites | Approved prerequisite behavior works through its owning compiler/runtime paths, with meaningful regressions and declared target coverage. | Visibility and shifts implemented locally; strings and checked arithmetic pending |
| P3 | First portable package batch | Each approved subset imports, compiles, runs, documents and installs through the SDK; fidelity gaps and friction are recorded. | Waiting on P1/P2 |
| P4 | Further package batches | Repeat P1-P3 for data structures, data formats, host services and selected vendor bindings. | Queued |

The next action is to checkpoint this work and integrate freshly fetched main
in the isolated worktree. Discuss each subsequent step's semantics before its
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
| Exported generic specialization | Slice algorithms and generic containers | Needs a separate design and visibility/identity rules | Before generic package APIs |
| Non-capturing procedure values | Comparator/predicate APIs | Needs a separate lifetime and capability design | Before callback-based algorithms |
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
- [PR 569](https://github.com/waldnercharles/chibi/pull/569) and
  [PR 571](https://github.com/waldnercharles/chibi/pull/571) concern prebuilt
  third-party archives and SDK distribution. They do not establish public
  `vendor:` language bindings. Coordinate acquisition/install changes with them.
- [PR 600](https://github.com/waldnercharles/chibi/pull/600) is merged in the
  planning base. Measure performance against that implementation.
- `cwaldner/m5-package-imports` at `8cb2911ec` is an older unmerged alternative.
  Compare its behavior with the present resolver before considering any reuse.

Preserve existing holds and review requirements. Study overlapping branches;
import them only when their actual changes are needed and eligible.

## Verification

Follow [testing policy](testing.md). Test execution has a hard 60-second limit;
build/configure time is separate. Use the owning C/pico_unit dispatchers:
`test/language/package_core.c`, `package_imports.c`, `packages.c` and
`package_compiler_chibi.c`, plus runtime tests where representation changes.

For each selected package or prerequisite:

- Establish a failing-before regression for a defect, and exercise public
  behavior rather than reproducing implementation details.
- Compare stage zero and stage one where the package must support both.
- Run relevant bytecode/compiled-backend cases and the admitted Wasm checks
  affected by the contract. Record unavailable required coverage as a gap.
- Verify installation and an external import for a newly shipped package.
- Measure representative text/container workloads when complexity or allocation
  is part of the contract; correctness tests alone do not establish performance.
- Inspect the combined diff and generated documentation before publication.

Library growth already consumes compiler-test budgets. Select bounded,
representative acceptance workloads and retain full-package import coverage
through admitted checks. Quarantined suites and deferred host bootstrap runs
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
60-second supervisor:

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
build and self-checks, took 29.089 s under an outer 60-second deadline.

The checked-in artifact files are modified locally with the accurate revision
`4a751bd5fb2bb414ec867f089acae9b9736e1d9e-dirty`. Regenerate from the final clean
candidate before publication. No commit, push or merge occurred. The historical
HEAD-export recovery selector, Windows, browser-host execution and quarantined
full suites remain unrun. String storage, default checked arithmetic and lexical
wrapping are still pending; this checkpoint does not claim those prerequisites.

Generated scratch artifacts were removed after copying the verified pair into
`tools/compiler/bootstrap`. Task failure/build logs remain in
`.chibi/odin-package-foundations`; regular native/Wasm build directories remain.
No scratch executables or project copies remain; normal `build-release/`
outputs and `.chibi/logs/` build logs are retained.
