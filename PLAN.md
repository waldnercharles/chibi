# Chibi toolchain consolidation and self-hosting

## How to execute this plan

This is the active implementation plan for the `chibi` repository. A request to
"Implement PLAN.md" means:

1. Read the repository `AGENTS.md`, this file, and the documents named under
   [Required context](#required-context).
2. Inspect the current branch, HEAD, uncommitted changes, `origin/main`, and
   relevant open pull requests before editing.
3. Continue the first milestone whose status is **Ready** or **In progress**.
   Finish one reviewable increment at a time. Do not skip ahead to a milestone
   whose dependencies are incomplete.
4. Run that milestone's checks under the repository's current test policy.
5. Update the milestone ledger and [Current checkpoint](#current-checkpoint) in
   the same commit. Keep only current evidence here; Git retains old checkpoints.
6. Continue with the next ready milestone while time and context allow. If work
   must stop, leave a clean, committed checkpoint with one explicit next action.

Routine implementation decisions are authorized. Ask before changing the stable
command meanings, language semantics, public package ownership, publication
visibility, or another user-visible decision recorded below. Creating or merging
a pull request, enabling GitHub Pages, publishing a release, and removing a
recovery path still require the authority normally required for those actions.

## Objective

Chibi will be the source language for its compiler, build logic, first-party
engine modules, documentation generator, and game behavior. Chibi must compile
Chibi. The normal build must not require project-owned Python, shell, CMake, or C
implementations once their Chibi replacements have passed the deletion gates.

The shortest path to that goal takes priority over adding commands. The existing
C compiler and CMake build remain stage-zero recovery tools until a Chibi compiler
can reproduce itself and a clean-checkout recovery procedure has been proven.
LLVM, SLJIT, platform SDKs, linkers, SDL, physics libraries, codecs, and similar
external dependencies may remain behind narrow bindings. Project-owned policy
and orchestration belong in `.chibi`.

Chibi remains worth building only if it delivers its distinct contract: safe,
opinionated game authoring with language-level entities, components, systems,
queries, effects, and assets. It should not grow into a second general-purpose
Odin. Odin is a design and repository-organization reference, not another
implementation language for Chibi.

## Definition of complete

The plan is complete when all of these are true:

- A stage-zero compiler builds a stage-one compiler from `.chibi` sources, and
  stage one builds a semantically equivalent stage-two compiler from the same
  sources. The normal compiler and build path uses the Chibi implementation.
- A clean checkout can build the compiler, runtime, SDK, documentation, and a
  game through Chibi-owned build logic with declared external dependencies.
- The installed SDK does not require project-owned Python, shell, or CMake to
  build or export a Chibi game. Required downstream compatibility files may be
  generated for consumers while those consumers still need them.
- First-party engine behavior is authored in `.chibi`. Remaining native code is
  generated glue or a narrow binding to an external ABI, with ownership and
  lifetime tests at the boundary.
- The stable commands below are implemented, documented, and covered by bounded
  process-level tests. Retired aliases and implementations have no callers.
- One allowlisted GitHub Pages site contains the overview, testing guide, FAQ,
  grammar, toolchain guide, checked examples, and generated package reference.
- A documented, checksummed bootstrap artifact can recover the self-hosted
  compiler from a clean checkout. The old C compiler is no longer on the normal
  path and can be removed from the active source tree after that recovery proof.

## Decisions that implementation must preserve

### Stable command interface

| Command | Contract |
| --- | --- |
| `chibi build [PROJECT]` | Compile a project into its cached runnable artifact without starting the player. |
| `chibi run [PROJECT] [--] [PLAYER OPTIONS]` | Build and run a project. Arguments after `--` belong to the player. |
| `chibi check [PROJECT]` | Parse, resolve, and validate without migration, formatting, or published build artifacts. |
| `chibi test [PROJECT]` | Discover and run authored Chibi tests under the shared execution policy. |
| `chibi fmt [PATH...]` | Apply canonical Chibi and authored-asset formatting; `--check` writes nothing. |
| `chibi doc [PROJECT]` | Generate the documentation site; previewing is an explicit option, not the default. |
| `chibi web [PROJECT]` | Build a browser target and serve it on loopback for local development. |
| `chibi export [PROJECT]` | Assemble a distributable native game or static browser site without serving it. |
| `chibi version` | Print compiler, SDK, build, and artifact-format identity. |
| `chibi doctor` | Diagnose installation and target capabilities without mutating the project. |

There is no project-facing `package` command. `export` is the game-distribution
operation. SDK, player, Studio, and release packaging are maintainer build tasks
until the Chibi build program owns them. There is no `serve` command; browser
development is `web`. Asset import and schema migration stay with the owning
asset or project workflow instead of becoming permanent top-level commands.

The unnamed `chibi [PROJECT] [PLAYER OPTIONS]` run form is a temporary migration
alias. Named commands take precedence, so a project directory named `build` or
`run` must be passed as `./build` or `./run`. Remove the alias after checked-in
callers and onboarding use `chibi run`.

### Language and package ownership

ECS declarations, entity identity, component and system syntax, queries,
presence proofs, effects, borrowing, and reduction rules are language contracts.
Games do not import an ECS package to use them. A future `core:ecs` package may
expose explicit world construction, inspection, and serialization, but it must
not redefine language semantics or introduce a second entity type.

The intended public collections are:

```text
core:       portable language and explicit runtime support
game:       first-party rendering, input, audio, physics, assets, and UI
vendor:     optional low-level bindings to external native libraries
```

Public package execution is not implemented yet. Current package syntax and the
private graph are foundations, not evidence that authored imports work.

### Bootstrap terms

- **Stage zero** is the existing C compiler and native build used to create the
  first Chibi-authored compiler. It remains a recovery path during migration.
- **Stage one** is the compiler program whose source is `.chibi` and whose
  executable artifact was produced by stage zero.
- **Stage two** is produced by running stage one on the same compiler sources.
- Self-hosting requires stage one to compile the complete compiler sources and
  produce a stage-two compiler that passes the same semantic fixture set. A
  Chibi script that invokes the C compiler is useful build orchestration, but it
  is not self-hosting.
- Binary equality is required only after inputs and outputs are deterministic.
  Semantic, diagnostic, ABI, and artifact-format equivalence are always required.

## Required context

- Read [`docs/self-hosting-build.md`](docs/self-hosting-build.md) before changing
  the build host, bootstrap stages, compiler port, or backend policy. Its process
  contract covers argv, cwd, environment, ownership, deadlines, output bounds,
  cancellation, and process-group cleanup.
- Read [`docs/toolchain-inventory.md`](docs/toolchain-inventory.md) before
  changing or deleting Python, shell, Make, CMake, packaging, reflection, test
  supervision, browser, or release code. Every deletion must satisfy the caller
  and equivalence gate recorded there.
- Read [`docs/testing.md`](docs/testing.md) before adding or running checks. Full
  native, render, Studio, and other quarantined suites are disabled, not passed.
- Read the current language architecture and contracts before compiler work:
  [`docs/code-language.md`](docs/code-language.md),
  [`docs/language-contracts/v1.md`](docs/language-contracts/v1.md), and the live
  sources under `src/language/`.

These documents explain existing contracts. This file alone owns milestone
order, current status, and the next action.

## Integration rules

Before each milestone, fetch `origin`, compare the branch with `origin/main`, and
inspect live pull-request heads that touch the milestone. If main advanced,
preserve current work in reviewable commits, then use a normal merge or a safe
rebase of unpublished commits. Never import an open branch merely to pre-resolve
a possible conflict.

Before publication, repeat that check, reconcile any landed overlap, review the
complete combined diff, and run affected admitted checks on the exact candidate.
Attribute old results to the tree that produced them. Rerun a passing check only
when integration or later edits changed the contract it covers.

Current integration constraints:

- Engine PR #436 landed on main at `7e900d956`. This branch integrates its
  retired-selector removals, Terrain Lab admission, documentation changes, and
  empty-mesh fix. Preserve those changes when editing test policy or fixtures.
- Engine PR #438 landed on main at `6ea2221c9`. This branch integrates its
  release-build correction for terrain-volume assertions.
- Engine PR #420 is a conflicted language and typed-runtime redesign. Recheck its
  authoring commands, SDK manifests, language docs, plan, and test-policy
  contracts before modifying those areas.
- Engine PR #407 is conflicted and explicitly held. Do not absorb its operation
  contracts, commits, or dependencies as part of this work.

## Milestone ledger

| ID | Status | Depends on | Deliverable |
| --- | --- | --- | --- |
| M0 | **Complete** | current main | Explicit `build` and `run` commands over existing preparation and player behavior. |
| M1 | **Complete** | M0 | A private build-only host that executes real direct-argv processes for an authored Chibi program. |
| M2 | **Ready** | M1 | A Chibi build driver that configures and builds the current stage-zero toolchain without shell-owned policy. |
| M3 | Blocked | M2 | Executable multi-file packages and the minimal `core:` support needed by compiler sources. |
| M4 | Blocked | M3 | One real compiler component, package graph and ordering, ported to Chibi with differential fixtures. |
| M5 | Blocked | M4 | Complete stage-one and stage-two Chibi compiler bootstrap. |
| M6 | Blocked | M5 | Chibi-owned internal build graph and installed SDK build path; CMake retained only for active consumers. |
| M7 | Blocked | M3 | Stable authoring and distribution commands implemented through shared Chibi modules. |
| M8 | Blocked | M3, M7 | Chibi documentation generator, public source allowlist, and reviewable Pages workflow. |
| M9 | Blocked | M5, M6 | First-party engine modules moved to Chibi packages behind tested native bindings. |
| M10 | Blocked | replacement-specific | Python, shell, CMake, and C implementations removed one responsibility at a time. |

M5 is the primary product goal. M7 through M9 may proceed in parallel only when
they do not delay or destabilize the active bootstrap path.

When a milestone completes, mark each direct dependent **Ready** if all of its
listed dependencies are complete. Leave it **Blocked** if another dependency or
an explicit decision gate remains, and name that gate in its milestone section.

## Milestones

### M0. Name existing project behavior

Status: complete on this branch.

The launcher implements these forms while preserving existing artifact paths,
project discovery, compiler selection, freshness checks, and player arguments:

```text
chibi build [PROJECT]
chibi run [PROJECT] [--] [PLAYER OPTIONS]
chibi [PROJECT] [PLAYER OPTIONS]
chibi export [PROJECT] --output DIR [--web PLAYER] [--archive]
```

Named commands win over implicit project paths. `build` never starts the player.
`run` starts it only after successful preparation. Compiler exit status reaches
the caller unchanged, and a failed preparation never starts the player. A cache
hit reuses the artifact; a missing artifact or newer project/compiler input
rebuilds it through the existing `prepare` path.

Owning changes:

- `src/host/chibi_main.c`
- `test/tools/chibi_cli.c`
- `test/tools/main.c`
- `test/tools/CMakeLists.txt`
- `tools/test_policy.py`

The focused test uses fake sibling compiler and player programs. It covers
build-only operation, default project discovery, cache reuse, exact player
argument forwarding, implicit run, and compiler failure. It is a POSIX-hosted
process fixture. Do not infer Windows runtime coverage from it.

PR #436 is integrated. Its removed external-example sources and selectors remain
removed, while the CLI fixture remains registered. It remains a focused admitted
selector because `make test-tools` builds a runtime-only driver without the
public launcher. The M1 build-host fixture likewise uses its explicit route.

### M1. Execute authored build steps in a build-only host

Status: complete on this branch for Linux and macOS. Windows retains stage zero.

The private host, capability catalog, bounded process API and focused C fixtures
are implemented. Both real-process and original recording fixtures pass on
Linux and macOS; actual counters verify bytecode and SLJIT. The ordinary native
game catalog rejects both private capability names. See the current checkpoint
for exact coverage and the remaining platform limits.

Promote the recording-only `test/language/BuildWorkflow.chibi` proof into a
private tool host. The host executes direct argv without a shell and is never
added to normal game catalogs. Reuse the native process implementation where it
already satisfies the contract; keep language-facing policy in the authored
program.

Expected owning paths:

- `src/host/process.c` and `src/host/process.h`: reusable native spawn and cleanup
  mechanics, extended only for missing build-host contracts.
- `src/host/build_host.c` and `src/host/build_host.h`: new build-only capability
  catalog, retained argument/result ownership, and error mapping.
- `src/host/build_main.c`: new private executable that reads one ordinary `.chibi`
  source, prepares it with the private build catalog, and invokes one named
  zero-argument function returning `int`. Its temporary interface is
  `chibi-build-host --root DIR --source FILE --entry NAME --timeout-ms N`. It
  does not consume a game project or introduce a prepared-tool artifact format.
- `test/language/BuildWorkflow.chibi`: authored orchestration source.
- `test/language/BuildProcessFixture.chibi`: new source that invokes only a
  controlled test worker supplied through the fixture's private environment.
- `test/language/build_workflow.c` and a focused `test/tools` process fixture:
  preserve recording parity and exercise real-child behavior in a disposable
  root. Tests never configure or build the engine repository.
- `sdk/CMakeLists.txt` or the narrow owning manifest: build the temporary host
  during stage zero. Do not expose it as a public game command.

Required behavior:

- Preserve every argv element, including empty strings and shell metacharacters.
- Set an explicit working directory and an allowlisted environment without
  mutating the parent process. Preserve supervision metadata such as
  `CHIBI_TEST_DEADLINE` when present.
- Bound captured stdout/stderr and report truncation.
- Return normal child exits as `0..255` and signal termination as a negative
  signal number. Spawn failure, timeout, cancellation, invalid input, and host
  failure are capability errors with stable diagnostics, not invented exit codes.
- Apply one inherited deadline, then cancel and reap the complete process group.
  The test host uses the inherited shared deadline. An unsupervised invocation
  requires the finite `--timeout-ms` budget.
- Retain all borrowed Chibi values for the full child lifetime and release them
  on success, failure, timeout, and cancellation.
- Reject the build capabilities when an ordinary game program requests them.

Completion criteria:

- The same authored program executes through bytecode and SLJIT against the real
  build host with equivalent results.
- Bounded C cases cover cwd, environment, empty and special argv, output caps,
  spawn failure, nonzero exit, timeout, cancellation, and descendant cleanup.
- Existing recording-host cases still pass. The normal game catalog has no
  process capability.
- Linux and macOS implement and run the real-process cases. Windows keeps the
  stage-zero path until equivalent start, poll, stop, timeout, and process-tree
  behavior exists; record that gap and do not select the new host on Windows.
- Build the exact candidate separately. Each admitted execution command shares
  its inherited 60-second deadline. Changed C is formatted with clang-format 22,
  and an independent review finds no unresolved correctness or lifetime issue.

### M2. Drive the existing build from Chibi

Status: ready. This is the next action.

Write one installed `.chibi` build program that invokes the existing CMake and C
compiler steps through M1. It owns argument construction, profile selection,
sequencing, and failure reporting. At this milestone CMake still owns its current
build graph, so this is authored orchestration, not self-hosting and not authority
to remove CMake.

Start with native compiler and tool-host configuration. Add web, SDK, Studio, and
release profiles only after their existing contracts have focused equivalence
tests. Preserve explicit compiler and cache precedence from `tools/configure.sh`
and `tools/configure_native.sh`.

Done means the build program works from an installed SDK, can configure and build
stage-zero products in a clean temporary checkout, and reproduces cache-hit,
configuration-change, compiler-change, and failure behavior without shell command
strings. Keep Make targets as thin transition entry points until current callers
move to the internal Chibi build program. The public `chibi build` command keeps
its authored-game meaning.

### M3. Make packages executable

Extend the current package syntax and private graph into the smallest public,
multi-file package system needed by the compiler. This milestone owns import
resolution, cycle and duplicate diagnostics, stable identities, deterministic
ordering, incremental invalidation, and package-level lowering. It does not add
remote dependency management.

Add only the portable `core:` modules needed by compiler sources. Preserve ECS as
a language contract. Complete this milestone when ordinary authored package roots
compile and run on bytecode and SLJIT, installed SDK consumers can import them,
and the package graph and syntax fixtures remain bounded and deterministic.

### M4. Port the first compiler component

Port package graph construction and dependency ordering from the current C
implementation to ordinary `.chibi`. It is the selected first component because
it is deterministic, already has bounded adversarial fixtures, and is required by
the rest of the self-hosted compiler.

Run the C and Chibi implementations against the same fixture corpus. Compare
accepted graphs, stable identities, order, cycle diagnostics, duplicate handling,
and limits. Keep C authoritative until differential parity passes on bytecode and
SLJIT. Record each language gap exposed by the port as a separate, tested language
change instead of adding component-specific host behavior.

### M5. Bootstrap the complete compiler

Port the compiler in reviewable components, retaining Tree-sitter, LLVM, SLJIT,
the platform linker, and other external systems behind C ABIs where useful. The
`.chibi` sources own package loading, semantic analysis, diagnostics, lowering,
artifact construction, and compiler control flow.

Each ported component uses shared fixtures against the remaining C implementation.
Once the complete stage-one compiler works, run this sequence in a clean tree:

1. Use stage zero to build stage one from the Chibi compiler sources.
2. Use stage one to build stage two from the same sources.
3. Run the semantic, diagnostic, artifact, VM, and SLJIT fixture set with both.
4. Rebuild one checked game and compare observable behavior and declared formats.
5. Record all external inputs and produce a checksummed bootstrap artifact.

Make stage one the normal compiler only after this sequence passes on every
supported host configuration. Keep an explicit stage-zero recovery invocation
until the clean-checkout artifact recovery test passes.

### M6. Replace the internal build graph

Move product profiles, generated prerequisites, native/cross compilation,
include/link ownership, install layouts, dependency cache policy, and artifact
assembly into Chibi modules. Preserve installed CMake package files while Studio
or another supported consumer needs them; generate those files rather than using
them as the internal source of truth.

Native reflection remains a host tool during browser builds until its Chibi
replacement reproduces generated identities, layouts, bindings, and transactional
failure behavior. Complete M6 only after native, runtime-only, browser, installed
SDK, relocated SDK, and source-free consumer cases pass from the Chibi build.

### M7. Finish the stable command interface

Implement commands through shared modules, not separate scripts:

- `version` reads one generated build-identity document used by installed and
  build-tree products. Human output is stable; add `--json` only for a concrete
  machine caller.
- `doctor` performs non-mutating compiler, player, SDK, browser-target, backend,
  and platform checks. Report supported, unavailable, or broken and return nonzero
  only for a broken requested capability.
- `check` uses the same parser, package resolver, semantic analyzer, and native
  catalog as `build`, but writes no project files or published artifact.
- `fmt` reuses canonical source and KDL formatters. `--check` writes nothing and
  returns nonzero for noncanonical or invalid input.
- `test` runs a documented authored-test contract through the shared supervisor.
  Approve discovery and assertion semantics before adding language syntax.
- `export` moves current native/browser assembly behind Chibi modules.
- `web` exports to a host-owned temporary directory, binds to loopback by default,
  prints the URL, serves until interruption, and removes only its owned temporary
  files. `export --web` remains non-serving.

Remove the unnamed run alias only after repository, Studio, and supported project
callers use `chibi run`. Every command needs help, exit-status, installed-SDK, build-tree,
missing-capability, and failure-path coverage.

### M8. Generate and publish one documentation site

Use one source of truth for prose and package comments. Create an explicit public
manifest under `docs/` that names every Markdown page, generated grammar page,
package reference, checked example, and static asset. Generate into `build/docs`.
Never scan all of `docs/`: it contains historical evidence, research, local paths,
and internal implementation records.

The public navigation is Overview, Learn, Testing, Packages, Examples, FAQ,
Grammar, and Toolchain. `chibi doc` builds it through a Chibi generator and
`chibi doc --serve` may preview it on loopback. A Pages workflow may publish only
the generated allowlist artifact. Review that artifact for private paths and
internal material before enabling deployment from this private repository.

### M9. Move first-party engine modules to Chibi

Expose narrow bindings for platform, graphics, audio, physics, codecs, filesystem,
and other external libraries. Implement first-party game-facing systems and
packages in `.chibi`, starting with modules whose current C boundary and behavior
already have deterministic fixtures. Keep desktop and browser behavior aligned.

The target is Chibi-owned engine policy, not reimplementation of every external
library. Replace Cute only when Chibi packages over lower-level dependencies cover
its current rendering, audio, resource, input, UI, and browser responsibilities.
Keep Box2D for 2D and Box3D for 3D unless actual game requirements and measurements
justify a separate physics migration.

### M10. Remove replaced implementations

Delete one responsibility at a time. Before deletion, name the replacement,
enumerate every repository, installed-SDK, CI, release, and application caller,
and run the equivalence checks in `docs/toolchain-inventory.md`. Remove obsolete
wrappers and docs in the same change. A repository-local `.chibi` script does not
replace an installed tool until the installed SDK works without engine source.

Python may remain only where an external application requires its Python API,
such as Blender integration. Hosted workflow YAML, external compilers, linkers,
backends, and operating-system libraries are dependencies rather than failed
self-hosting. All Chibi project policy called by them belongs in `.chibi`.

## Current checkpoint

Last verified: 2026-10-17.

- Repository: `chibi`
- Branch: `codex/toolchain-cleanup-20261017`
- Starting checkpoint: `9ea13c24bce7c470ac1cde30b2ec8f714e6f33fd`, M0
- Integrated upstream base: `origin/main` at `6ea2221c97d266c9bb028443cb67bd445fdb3476`
- Latest fetch and merge check: upstream unchanged; already up to date
- Completed milestones: M0 and M1
- Next action: M2, implement the installed Chibi program for native stage-zero
  configuration/build, preserving compiler and cache precedence

M1 adds `src/host/build_host.c`, `build_host.h`, `build_main.c`, the bounded
capture API in `process.c`/`.h`, and the private stage-zero CMake targets.
`test/tools/build_process.c` and `BuildProcessFixture.chibi` cover the real host;
the original recording source and cases remain unchanged. No public package,
game catalog, project format, normal command meaning or recovery path changed.
The host is not installed into the SDK yet; installed orchestration belongs to M2.

Current candidate evidence:

| Check | Result |
| --- | --- |
| `make build BUILD_TARGET='chibi chibi-build-host test_tools'` | Passed; final configure 3.3s, build 1.6s, separate from execution. |
| `python3 tools/test_policy.py tool build-process`, macOS | 10 cases, 2,155 assertions, 5.054s. |
| `python3 tools/test_policy.py tool build-process`, Linux | 10 cases, 2,154 assertions, 1.673s. |
| `python3 tools/test_policy.py tool language-build-workflow`, macOS | 8 cases, 90 assertions, 1.228s. |
| `python3 tools/test_policy.py tool language-build-workflow`, Linux | 8 cases, 90 assertions, 0.051s. |
| `python3 tools/test_policy.py tool chibi-cli` | 1 case, 22 assertions, 1.381s. |
| `make test-native` | Passed, 24.803s execution. |
| `make test-materials` | Passed, 28.982s execution. |
| `make test-projects` | Passed, 31.079s execution. |
| `make test-language-all` | Passed, 18.265s execution. |
| `make test-tools` | 125 cases, 27,270 assertions, 25.426s execution. |
| Changed C files, clang-format 22; `git diff --check` | Passed. |

Every execution route uses the production inherited deadline, with a 60-second
ceiling per admitted command. macOS uses the native Debug build with assertions,
ASan/UBSan and the installed LLVM compiler. The standalone complete tooling group
uses its own Debug configuration. Python 3.12.14 was selected through
`PATH=/opt/homebrew/opt/python@3.12/libexec/bin:$PATH`.
Linux uses Clang 21.1.2, Debug assertions and Python 3.12.15 on the existing
Rancher aarch64 VM, without sanitizers or native/desktop SDK packages. The macOS
case checks the full ordinary game catalog; Linux's minimal catalog rejects the
private names. Both execute real bytecode and SLJIT and verify backend counters.

Independent review found and resolved argv-zero rewriting and an invalid test
binding. Execution exposed two macOS differences, now covered by the real-child
cases: Python's uptime clock differs from `CLOCK_MONOTONIC`, and Darwin can return
`EPERM` for a group containing only a completed, unreaped leader. Final review
found no unresolved correctness or lifetime issue. Process supervision covers
ordinary same-credential child groups; it is not containment of children that
escape their group or change credentials.

Logs, configuration commands, review notes and checksums are retained under
ignored `.chibi/evidence/toolchain-m1/`. Linux used only cached dependency source
and disposable build directories. No engine project was configured or built by
an authored test. Windows process-tree equivalence remains unimplemented, so its
stage-zero path stays selected. Full native/render/Studio/browser/device and
other quarantined suites were not run and are not passed. Repository-wide format
was not rerun; only changed C files were checked.
