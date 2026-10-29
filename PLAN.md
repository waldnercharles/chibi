> Current sequence decision (2026-10-22): ordinary arrays/maps/builders are
> independent values with copy-on-write; explicit pointers share mutable access.
> This supersedes the earlier shared-array draft below. See
> [the revised sequence plan](docs/value-sequences.md). Mutable views require an
> explicit array pointer and follow its current pointee at fixed indices. The
> earlier draft source is preserved and unpublished.

# Chibi toolchain consolidation and self-hosting

## Coordinated named struct results, 2026-10-22

The next local setup slice extends the ABI20 authored compiler's existing
named-result protocol to structs. Terrain Lab requires a named `CF_V2` result
that returns an ordinary expression and is consumed directly. The cutover owner
approved this scope; the managed-pointer owner reserves pointer recognition and
requires pointer named results to remain rejected. No ABI21 code or independent
bootstrap regeneration belongs in this slice.

Native semantic/lowering behavior and the existing runtime result contract are
the source of truth. Named record bindings need typed, owned zero storage before
field writes; returned headers must preserve the nominal record and reach the
correct result ordinal. Reuse the existing snapshot, result-cell and caller
storage protocol. A singleton result works as its ordinary value while keeping
the named projection valid. Explicit named return values are assigned in order
before publication, as the native compiler does. Do not change native defaults
or missing-return semantics based on assumptions.

Changes span `Compile.chibi` signature/result headers, `Body.chibi` prologue and
result bindings, and `Steps.chibi` returns, caller cells and projections.
`test/language/package_compiler_chibi.c` owns native differential regressions for
single scalar and record results, bare record returns and field mutation,
multiple result projections, sequential assignment, nested string bytes and
caller/callee copy independence. The singleton and record samples both fail on
the current compiler before implementation. Keep helper source provenance
explicit rather than overloading the nominal record index.

After implementation, verify package and behavior results, debugger points and
watches on VM and the platform compiled backend; then self-compilation,
source-only recovery, formatting, independent review and the five required Make
groups. Probe Terrain Lab again to identify its next actual blocker. Keep the
installed default unchanged until the combined compiler and SDK support the
representative projects. No project version migration is required.

The named-result implementation passes package, struct and owned-string
differential checks on VM and SLJIT. Singleton projections follow declaration
initializers through lexical aliases, matching native semantic analysis, without
adding provenance to every compiler value or name. A widened local's projection
converts to the declared result type using the existing checked conversion.
The same repair in native lowering resolves its physical/declared watch-type
mismatch; valid boundaries succeed and out-of-range values fail on both paths.
PR #660 is merged and supplies member-procedure effect propagation. The combined
behavior fixture then exposed a separate source-semantic rule: the native
compiler marks member access on a component-typed local as READ, even though the
local access creates no ECS component dependency. Preserve that semantic flag
separately from actual reached/written component tables and feed it into the
existing effect fixed point. A private work directive collects these flags in
the body summary without enlarging emitted instructions or changing runtime
validation accounting. Bare typed result construction remains pure.

The independently reviewed empty-project route is included in this candidate.
`--stage-one-input` reports the exact executable-source count after writing its
input; zero sources select explicit `--resources-only` assembly before compiler
recovery. Assembly rescans and rejects any executable source, preserving prior
output on failure. Empty closures remain invalid. The focused C integration
compares artifact/resource bytes and CPU traces with native preparation while
the behavior compiler is unavailable.

The compiler owner approved regeneration of the combined ABI20 bootstrap after
source freeze. Required Make groups, all focused compiler selectors, independent
review and recovery with the checksum-pinned runtime gate this checkpoint;
three-platform pinned-runtime CI gates landing. Rich game native catalogs follow
in a separate coordinated increment. The installed default remains unchanged.

The frozen checkpoint passes `make test-native test-materials test-projects
test-language-all test-tools`: 1,561 cases and 1,568,825 assertions, no failures,
in 865.3 seconds including configuration and builds. All thirteen focused
compiler selectors pass (14 cases, 111,175 assertions, 188.653 seconds under
concurrent workloads). The explicit empty-route integration passes 3 cases /
20 assertions in 21.7 seconds including its supervisor; `make check-format`
passes in 80.3 seconds. Bootstrap generation passes in 133.9 seconds including
configuration and builds. The exact pinned macOS runtime, with no native
sources and an empty child PATH, recovers byte-identical stages of 12,098,990
bytes, SHA256
`41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e`.
Its initial two-stage proof took 34.792 seconds under concurrent workloads.
Three-platform CI is still pending; these local results do not establish Linux
or Windows recovery, or a full self-hosted game/default cutover.

## Coordinated entity expression metadata, 2026-10-22

Terrain Lab now reaches `terrain = globals.Terrain` and rejects it because the
state binding drops its declared entity record while the keyed global keeps it.
Native entities are nominal handles. Preserve that identity in retained-state
and `self` bindings and in native/ordinary field-read values; keep exact type
comparison and native result headers unchanged. Verify global assignment,
comparison with `self`, null/zero reset and rejection of a distinct handle type
through the existing behavior differential dispatcher, including source watches.

This is a narrow ABI20 producer/binding repair in the named/entity slice. Rich
nominal native-port catalogs, native result buffers and provenance belong to
the next coordinated compiler increment. Keep the pure bootstrap Port/target
JSON and its twelve-argument exports unchanged; regenerate only the matching
combined compiler artifact after validation.

## Coordinated behavior-state support, 2026-10-22

The local setup chat owns authored behavior state for bool, entity and string,
including literal inference, valid defaults, nominal identity and retained string
ownership. The compiler cutover chat owns CLI/default selection, build-program
compilation and SDK wiring. The managed-pointer chat owns its unmerged ABI21
changes; this increment remains on ABI20 and does not consume that branch.

Implementation is limited to `package_compiler/Behavior.chibi`, its state default
emission in `Compile.chibi`, and state assignment, entity literal/equality and
floating literal coercion paths in `Steps.chibi`. `Body.chibi` includes null
expressions in its debugger dispatch.
`test/language/package_compiler_chibi.c` compares declarations, debugger metadata
and runtime state against the native compiler on VM and SLJIT. Empty-project
handling must agree with the cutover owner's resource-only assembly contract;
the existing nonempty compiler-closure format must not change implicitly.

Verification starts with failing behavior regressions, then the affected
compiler behavior/package checks, self-compilation and current compiler recovery.
Use byte-preserving string comparisons and verify state after handler return and
later invocations. Bootstrap regeneration belongs to the coordinated compiler
candidate. Main checkouts and the installed native default stay usable until
the completed cutover is validated. No changes in this slice require a project
version migration.

The macOS checks passed all five required Make groups: native, materials,
projects, language and tooling (1,561 cases, 1,568,825 assertions). Independent
review is clear. The canonical compiler sources recover outside the checkout
with an empty PATH and unavailable C/C++ compilers; the two compiler stages
reproduce 11,740,548 bytes with SHA-256
`14db7d8be677859cbd451cbbee7e225114cf65e6abaaf462a03e8bd88a21076a`.
This slice does not establish Linux, Windows or combined SDK/Studio coverage.

Terrain Lab now passes its state declarations and next rejects
`DigPoint :: () -> (Point: CF_V2)` at `behaviors/Explorer.chibi:26:24` because
named struct results are not supported. The cutover owner retains this follow-up
and combined-candidate validation. The local default is not changed by this
slice, and the checked-in bootstrap is untouched.

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
6. Push this branch to `origin` after each completed, reviewed and validated
   milestone, as requested by the owner on 2026-10-17.
7. Continue with the next ready milestone while time and context allow. If work
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

The owner's 2026-10-18 acceptance requirement further constrains the final
normal installed workflow: Chibi must compile Chibi and drive build/link without
requiring `cmake`, `clang`, or `gcc` executables. Calling hidden or renamed copies
of those tools does not meet this requirement. Native backends, platform SDKs,
assemblers/linkers and runtime libraries may remain explicit external inputs
behind narrow bindings; record exactly which are still needed. Stage-zero
construction and recovery may use the current native tools during migration.
The final installed/relocated clean-checkout proof must run with those three
executables unavailable, exercise the complete compiler sources through stage
one and stage two, and build/link a checked game. No ported component or build
wrapper alone establishes that acceptance condition.

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

The unnamed `chibi [PROJECT] [PLAYER OPTIONS]` run form was a temporary
migration alias. It was removed on 2026-10-19, once no checked-in caller,
Studio (which runs only `chibi export`) or supported project used it; `chibi`
without a command prints its usage, and an unknown command fails.

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

The SDK can analyze and execute explicitly supplied multi-file packages and their
imports. An owned resolver snapshot resolves imports against explicit collection
roots; prepared generations own exported declarations and linked dependencies.
A bounded disk adapter supplies explicit package roots on Linux and macOS. The
installed `core:strings` collection and pure native arrays support the first
compiler component; Windows disk loading remains unsupported.

The owner approved the Odin-inspired package library program on 2026-10-20,
with fidelity decided per package and useful language prerequisites implemented
before affected ports. [Package library plan](docs/package-library-plan.md)
owns that program's sequence, semantic decisions and implementation status.
Read it before expanding `core:` or adding public collections. The toolchain
milestones below retain their existing order and acceptance conditions.

The owner approved managed pointers with automatic lifetime and cycle cleanup
on 2026-10-22. [Managed pointer implementation](docs/managed-pointers.md) records
the contract, compiler/runtime work and current validation evidence. It builds
on explicit call-site `ref` while keeping ordinary value copies independent.

The current prerequisite is the approved byte-preserving string representation:
one immutable public `string` carrying bytes and length, with explicit UTF-8
operations. This branch implements descriptors and ownership throughout the
compiler, execution backends, persistence, and native adapters. It diagnoses
provably invalid embedded NUL at C-only uses during compilation and validates
unknown values before native effects. Existing boolean APIs return failure and
existing diagnostic APIs fail the invocation. New APIs accepting external data
must expose recoverable errors. Shared slices follow the descriptor cutover.
Track language friction and discuss each package's semantics before porting it.

The owner revised the collection decision on 2026-10-22: ordinary arrays, maps
and builders are independent values with copy-on-write storage. Explicit
pointers share mutable access. Range slicing copies into a new array, and
`clone()` allocates independent outer storage. `p.view(range)` requires an array
pointer and follows its current pointee at fixed indices, including after
reassignment. [Value sequence plan](docs/value-sequences.md) tracks implementation
and the managed-pointer dependency. This remains unpublished draft work.

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
- Read [`docs/testing.md`](docs/testing.md) before adding or running checks.
  Select coverage from the affected contracts and report what actually ran.
- Read the current language architecture and contracts before compiler work:
  [`docs/code-language.md`](docs/code-language.md),
  [`docs/language-contracts/v1.md`](docs/language-contracts/v1.md), and the live
  sources under `src/language/`.

These documents explain existing contracts. This file owns toolchain milestone
order, current status, and the next action; the package library program linked
above has its own ledger.

## Integration rules

Before each milestone, fetch `origin`, compare the branch with `origin/main`, and
inspect live pull-request heads that touch the milestone. If main advanced,
preserve current work in reviewable commits, then use a normal merge or a safe
rebase of unpublished commits. Never import an open branch merely to pre-resolve
a possible conflict.

Before publication, repeat that check, reconcile any landed overlap, review the
complete combined diff, and run affected selected checks on the exact candidate.
Attribute old results to the tree that produced them. Rerun a passing check only
when integration or later edits changed the contract it covers.

Current integration constraints:

- Engine PR #436 landed on main at `7e900d956`. This branch integrates its
  retired-selector removals, Terrain Lab admission, documentation changes, and
  empty-mesh fix. Preserve those changes when editing test policy or fixtures.
- Engine PR #438 landed on main at `6ea2221c9`. This branch integrates its
  release-build correction for terrain-volume assertions.
- Engine PR #420, the language and typed-runtime redesign, merged into `main`
  on 2026-10-19 at `07eb2abaa`. This branch integrates it: project format 27
  (main's v26 syntax step, then this branch's conversions step over a frozen
  v26 parser) and prepared schema 18. Stage one reads packed arrays and `for`
  loops, and its twins follow main's expression kinds and inferred purity.
  The bootstrap artifact is checked in at `tools/compiler/bootstrap` and was
  rebuilt from the merged tree (#512); the earlier published prerelease
  predates schema 18 and is refused.
  PRs #504 (`text_bytes`) and #509 (reference type checks), which landed on
  this branch during the integration, are merged in as well. #509's storage
  check now accepts two unnamed unions, because main's lowering gives union
  headers no declaration name.
- Engine PR #407 is conflicted and explicitly held. Do not absorb its operation
  contracts, commits, or dependencies as part of this work.

## Milestone ledger

| ID | Status | Depends on | Deliverable |
| --- | --- | --- | --- |
| M0 | **Complete** | current main | Explicit `build` and `run` commands over existing preparation and player behavior. |
| M1 | **Complete** | M0 | A private build-only host that executes real direct-argv processes for an authored Chibi program. |
| M2 | **Complete** | M1 | A Chibi build driver that configures and builds the current stage-zero toolchain without shell-owned policy. |
| M3 | **Complete** | M2 | Executable multi-file packages and the minimal `core:` support needed by compiler sources. |
| M4 | **Complete** | M3 | One real compiler component, package graph and ordering, ported to Chibi with differential fixtures. |
| M5 | **Complete on macOS arm64 and Wasm** | M4 | Complete stage-one and stage-two Chibi compiler bootstrap. Open gate: the Linux x86_64 and Windows x64 runs. |
| M6 | **In progress** | M5 | Chibi-owned internal build graph and installed SDK build path; CMake retained only for active consumers. |
| M7 | **In progress** | M3 | Stable authoring and distribution commands implemented through shared Chibi modules. |
| M8 | **Complete** | M3, M7, landing on `main` | Chibi documentation generator, public source allowlist, and reviewable Pages workflow. |
| M9 | Blocked | M5, M6 | First-party engine modules moved to Chibi packages behind tested native bindings. |
| M10 | **In progress** | replacement-specific | Python, shell, CMake, and C implementations removed one responsibility at a time. |

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
  mutating the parent process. Preserve process ownership and cancellation.
- Bound captured stdout/stderr and report truncation.
- Return normal child exits as `0..255` and signal termination as a negative
  signal number. Spawn failure, timeout, cancellation, invalid input, and host
  failure are capability errors with stable diagnostics, not invented exit codes.
- Apply the caller's explicit operation deadline, then cancel and reap the
  complete process group. The CLI requires a positive `--timeout-ms` argument.
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
- Build the exact candidate separately. Changed C is formatted with clang-format 22,
  and an independent review finds no unresolved correctness or lifetime issue.

### M2. Drive the existing build from Chibi

Status: complete on this branch for native Linux and macOS orchestration.
Windows retains stage zero.

The installed driver, private host primitives, cache probes and focused C
fixtures are implemented. The relocated SDK configures and builds compiler/host
products from a clean source snapshot and assembles a standalone SDK with verified
private-tool provenance. Python dependency/input helpers and the CMake graph remain
transitional dependencies. See the current checkpoint for exact coverage.

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

Status: complete on this branch for native Linux and macOS. Owned multi-file
packages and file-local imports execute functions, constants, records and unions
on bytecode and SLJIT. Canonical disk loading supplies complete explicit roots;
prepared generations retain owned exports and dependency modules, reuse exact
inputs, and invalidate transitive importers. Installed consumers import the
shipped portable `core:strings` collection and use pure generic native arrays.
Both relocated SDKs pass the package fixtures. Windows filesystem loading and
general authored generic specialization remain separate gaps; buffer-based
package APIs are portable. M4 is the next bootstrap milestone.

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

Status: complete on this branch for the shared package-graph corpus on bytecode
and SLJIT. The authored component matches C membership, identity, ordering,
bindings and diagnostics. Parser and owned-value fixes have separate commits and
regressions. The normal stage-zero compiler still uses C; no deletion is implied.

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

Status: in progress. Authored source snapshots compose syntax diagnostics,
header/import extraction, lexical paths, routing and package graphs. Active
constant admissibility and scalar folding operate on typed analysis facts.
Authored lexical binding now owns symbol lookup, declaration admission and
stable identity construction from explicit scope and declaration tables.
Nominal record/union headers preserve declaration placement, origin checks and
catalog metadata publication through the same authored lexical binding.
Named annotations resolve builtin, lexical, catalog and imported types, modifiers
and type-use records from raw analyzer/catalog facts.
Structural union, task and await constructors select types and publication
metadata from raw catalog, function and symbol tables.
Shared unit algebra preserves parsing, dimension equality, products, formatting
and overflow results using authored strings and term arrays.
Raw annotation and signature reading composes those modules from complete
syntax trees, preserving immediate constructor publication and type-use records.
Callable headers preserve sequential generic/parameter visibility, signature
metadata, membership and pending initializer/body references. Annotation reading
uses explicit traversal frames so nested types do not consume callable depth.
Bounded differential corpora pass on both backends. The actual eight-package
snapshot closure also roundtrips through prepared executable artifacts and runs
in fresh runtime-only processes. The normal compiler remains stage zero; this
is not a stage-one or stage-two bootstrap.

The private syntax-retention and byte-transport increment (PR #445) appends raw
Tree-sitter named and extra flags to the parser bridge and snapshot forest,
retains `Document.syntax_root` separately from routing `root`, and adds a bounded
owned `byte[]` return with an atomic `--output-artifact` handoff to the runtime-only
host. Its first local runs exposed three defects, each now covered by a case that
failed first. Preparation reserved a full live-value and root vector per
suspension, about 38 MB for `Snapshot.Build`, whose 943 pauses retain 978 values,
so the eight-package closure exceeded the host's 64 MiB aggregate. Live sets are
now counted after liveness and allocated exactly from each definition's bounded
owned arena. Module-set validation serialized dependencies through the public
prepared writer only to measure them; `execution_prepared_measure` now supplies
length and fingerprint, so the writer no longer links into `chibi-compiler-host`.
Two fixtures assumed MISSING-brace recovery and fixed-width prepared headers.
The transport fixture still echoes stage-zero-prepared bytes; it does not
establish authored serialization, a complete compiler, stage one or stage two.

The authored unit-analysis repair is merged in
[PR #440](https://github.com/waldnercharles/chibi/pull/440) at
`a5edefd3b79db958e444adc624df7ca8fa493e02`, tree
`ebfae34988e334412d2239e3b277475391898edc`. It replaces recursive evaluation with
explicit work/operand stacks and fixes zero-capability lookup after both defects
were reproduced on bytecode and SLJIT. The full authored unit differential is
green in [run 39112565011](https://github.com/waldnercharles/chibi-studio/actions/runs/39112565011);
its sanitized status JSON SHA-256 is
`3f290081324f0a89d1aae1fdd013ba7649ea4a5eac4e2046ffddf1a765218cfb`.
The fixture observer remains off by default. PR #443 connects the production
analyzer to the extracted C reference helper and passes its native selectors. It
does not move the normal compiler off stage zero or dispatch authored analysis
in production.

The authored type-relations component and its dedicated differential route,
`language-type-relations-chibi`, are merged in
[PR #442](https://github.com/waldnercharles/chibi/pull/442) at
`ace559c9c294a450c95a3a730dcd6d9f1b5ce13c`, outside aggregates.
Its four-case finite corpus passed 105 requests and 210 calls across bytecode
and SLJIT, with ten preparation parses and retained-result checks, in
[run 39113816903](https://github.com/waldnercharles/chibi-studio/actions/runs/39113816903).
The supplied 50,000 instruction argument is inherited/reset per activation,
not an invocation or global instruction ceiling. The tested source was
`6787ff61d3516ad4d9064ec34b16b5514233d64e`; the merge matches its exact tree
`95ddc9104ad6015f54c10fb59987ef31a65eaa34`. The sanitized status JSON SHA-256 is
`06053425ed3d257bb9d201194895f4e643c80bf0efa43edcf797bc150c617752`.
No native type-relation adapter or language syntax change is introduced.

The merged additive union-members, effects and record-members work wires
three separate full differential routes into the existing language driver,
with private C reference helpers and authored sources only. Normal production
analyzers and binding finalizers remain unchanged. Union members retains
24 requests, 48 calls and 32 parses with source/raw supplied budgets of two
million/sixteen million; effects retains 34 requests, 68 calls and ten parses
at 500,000; record members retains 26 requests, 52 calls, 32 parses and four
runtime JSON decodes at 500,000. All arguments are inherited/reset per activation,
not whole-call or aggregate instruction ceilings. Each route remains outside
aggregates. Hosted run
39138442670 at source `ec13962865b093bbeb2693a46ec6dc22fac6e498` passed
union members, record members, unit analysis and type relations, with execution
identity, expected pico summaries and production deadlines verified. Effects
failed during snapshot preparation before authored execution. An independently
admitted complete three-source C syntax diagnostic reproduced one error in the
Effects local declaration named `unordered`; Diagnostics and CoreStrings parsed
cleanly. The local and its sole use are now named `isUnordered`, preserving
reduction logic and all fixtures. Corrected-source syntax passed all three
complete-source cases and nine assertions in 0.072 seconds. That earlier run's sanitized status SHA-256 is
`6139c665be643f00f75827871fbb5b93c514f35d2d1888fb75a9fcc2fd56da86`.
The corrected source `b57ac78c53c170139d8f807158c076f971a2202b` subsequently
passed all five complete semantic selectors in
[run 39140946564](https://github.com/waldnercharles/chibi-studio/actions/runs/39140946564):
union 3 cases, effects 4, records 5, unit analysis 3 and type relations 4.
Execution identity and exact pico summaries were verified. Configuration took 3s and build 15s; integer-second execution
times were 7s, 1s, 1s, 3s and 0s respectively. PR #444 merged at
`c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb`; its tree exactly matches tested tree
`09d520fe2a5723226126b0879faaee390e87b7b9`. The sanitized status SHA-256 is
`7d85449277f819cf77949ad6fb2fec52c49923473bbea8aef069f1ff86dea84a`.
This evidence belongs to that semantic tree. The transport integration rebuilds
shared frontend and language-driver archives and still needs its own strict
product-link audit, both full transport selectors and all five complete semantic
selectors, with independent review of the combined contracts.
No raw hosted diagnostics or source contents are exported.
Those compiler-only results do not replace the native adapter build or tests.
Complete authored semantic analysis, lowering, artifact production, stage one
and stage two remain incomplete; these components do not establish self-hosting.

The Bool/I32 evaluator-lowering component adds the focused
`language-scalar-lowering-chibi` differential. Chibi owns the admitted
typed-expression traversal and plan emission. The native adapter decodes that
plan and retains builder verification, optimization, finalization, liveness,
borrowing, runtime rewriting, parallel proof, definition snapshots, module
publication, serialization and backend emission. Its private reference oracle
shares the production value-evaluator emitter. The differential passes 1 case /
33,382 assertions; binding-schedule (5 cases) and binding-benchmark pass on native
and Wasm, as does the complete Wasm group.
The inactive exact-rational draft remains unused. Full-size scalar
evaluation-budget exhaustion needs separate resource admission. Nested authored
record fields remain unsupported; these components use flat fields and tagged
payloads.

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
5. Record the exact bootstrap compiler revision, minimum supported bootstrap
   version and external inputs, then produce a checksummed bootstrap artifact.

Introduce compiler language dependencies in two steps: first implement a feature
using the existing bootstrap language and validate the resulting compiler; only
then use that feature in later compiler sources. Keep the compatible bootstrap
artifact available and reject unsupported bootstrap versions explicitly.

Step 5 is implemented for the package compiler on 2026-10-19:
- `make bootstrap-artifact` writes stage two as a checksummed artifact, with a
  manifest recording:
  - its revision;
  - its bootstrap version and minimum, from `tools/compiler/bootstrap.kdl`;
  - each source's SHA-256;
  - the external inputs: the value layout, prepared ABI, instruction names and
    native identities.
- `chibi-compiler-host --bootstrap` recovers the compiler from it. It rejects
  another format, a damaged closure, a missing source or grown package,
  another target and a version older than the sources' minimum. It then requires the artifact's own bytes, or a
  fixpoint when the sources changed.
- `make test-recovery` runs that recovery from sources exported from HEAD in
  about 18 s: one compile, or two when the sources changed.

At the owner's direction on 2026-10-19 the clean-checkout artifact is checked
in at `tools/compiler/bootstrap`, as Zig checks in `zig1.wasm`, replacing the
earlier release-asset download. It was rebuilt on 2026-10-20 from revision
`94858a96c`, after #420's syntax and prepared target reached the compiler
sources; the artifact published before that is refused ("prepared target ABI
differs"). `make bootstrap-recover` recovers the
compiler from it and the sources on disk without a network. See
`docs/self-hosting-build.md`.

Make stage one the normal compiler only after this sequence passes on every
supported host configuration. The Linux x86_64 and Windows x64 runs wait for
CI or a bigger host (owner decision, 2026-10-20); macOS arm64 and Wasm pass. Keep an explicit stage-zero recovery invocation
until the clean-checkout artifact recovery test passes.

The 2026-10-22 compiler self-build CI removes native runtime construction from
the normal proof. It starts from a checksummed runtime binary recorded in
`tools/compiler/runtime.json` and the checked-in compiler artifact, compiles
the current compiler sources, and executes the resulting compiler to compile
them again. No C compiler or CMake is installed or invoked by that workflow;
native runtime-seed construction is a separate manual recovery operation.
This proves compiler-artifact self-reproduction on the supported native hosts.
The final installed build/link and checked-game acceptance above remains a
separate gate; the native runtime itself is still an explicit prebuilt input.

### M6. Replace the internal build graph

Compiler-link transport, 2026-10-28: the authored graph writes GNU response
files for compiler-driver link execution while retaining full argv in command
databases and cache records. The native argument admission limit is unchanged.
A real Clang fixture exceeds that limit with UTF-8, quoted and empty arguments,
then checks cache hits, input/argument invalidation and failure/retry cleanup.
The native test group carries the host from its selected build directory and
preserves explicit host overrides. Broader caller/platform proof remains a
separate gate from the focused local control.
Older Clang GNU response parsers discard quoted empty arguments. The transport
keeps those arguments directly in argv and compresses each surrounding nonempty
run only when its response path is smaller. Argument count and byte use never
increase. The readable authored fixture covers empty option operands, numbered
response failure/cleanup and interrupted-attempt remnants on Clang 21, 22 and
23. It also avoids the oversized C literal rejected by Clang 18 in CI.

SDK version ownership, 2026-10-28: `sdk/VERSION` is the shared version authority.
The graph identity no longer parses a CMake assignment; retained CMake SDK and
consumer-version generation read the same file and track configuration changes.
The value remains `0.1.0`. This does not replace SDK construction or installation.
Local macOS validation passes the shared-authority/reconfiguration and existing
resource/corpus identity cases, 3 cases and 20 assertions in 44.42 seconds outer
wall time. CMake identity and package-version output are byte-identical before
and after extraction. Independent review, clang-format 22 and diff checks pass.

Linux native route: `GRAPH_BUILD=1` now selects the existing graph for the full
products and tooling/language drivers. Linux link inventories follow the pinned
archive; full compile/link, generated-header, corpus/resource and discovery
checks apply on Linux as on macOS. The Linux default remains CMake pending the
explicit `ci-linux-graph` label or manual Linux Graph validation gate. The
proof adds one Ubuntu job only when requested, with two build workers and a
45-minute ceiling: full Release/Debug builds, a small CMake reference, parity
and the existing four native groups. Ordinary PR/push CI gains no duplicate
suites; the later default cutover must replace the native build lane rather
than retain a second full lane. Hosted wall time remains to be established.
The first-host bootstrap/reference, standalone test
consumers, public SDK, web and Studio remain explicit CMake responsibilities;
this slice is not total CMake/Ninja removal.

Compiler-tools packaging slice, 2026-10-22: `tools/toolchain/SDK.chibi`
installs graph-built private hosts and their complete compiler/core/bootstrap,
authored build programs, graph packages and matching dependency notices into a
fresh relocatable prefix. Private directory reservation and publication refuse
existing paths. The graph's explicit profile emits commands without toolchain
discovery. This working slice now includes main `5c28191b3e` and its ABI20
compiler resources; it does not regenerate the bootstrap. The macOS packaging,
host, graph parity and five required groups pass. The reviewed source trees,
format-only refresh, binary identities, reserved files, Linux recipe and
remaining full-SDK/platform gates are in
[`docs/design/graph-sdk-packaging.md`](docs/design/graph-sdk-packaging.md).

Status: ready (owner decision, 2026-10-21). M5 is complete on macOS arm64 and
the 32-bit Wasm host; its Linux x86_64 and Windows x64 bootstrap runs stay an
open gate that can run later from the checked-in bootstrap artifact or any
commit's. Until each of those platforms passes its run, M6 must not remove
stage zero, recovery, or a build path that platform uses, and Windows keeps
its CMake and stage-zero path because the build host has no Windows process
support yet.

Approach (owner, 2026-10-21): the graph is Chibi code the build host runs
itself, with no Ninja. `build.start` and `build.wait` run compiles in
parallel (#612); `-MD` dependency files and recorded stamps decide rebuilds;
targets are functions over typed records, as Jai's `BuildCpp` and `build.zig`
are; each target's compile commands must equal CMake's
`compile_commands.json` apart from output paths, and its result must link.
Later slices that compile Chibi should call the compiler in process, as Jai's
workspaces do, rather than start `chibi-compile`.

Slice 1, 2026-10-21: the `graph:` collection (`tools/graph`, with
`--collection` on the build host) builds `chibi_support` on macOS and Linux
through `tools/toolchain/Graph.chibi`, beside CMake. `build-graph` checks its
five compile commands against CMake's, links its archive, and checks rebuilds.
Slice 2, 2026-10-21: the graph also owns the libraries that use only
`chibi_support` (`chibi_types`, `chibi_asset_values`, `chibi_text_search`,
`chibi_ui_document_values`), with private headers and stale-header removal.
It compiles across libraries in one job window, writes
`compile_commands.json` on every run, and builds a subset with `--target`.
Slice 3, 2026-10-21: `chibi_value_document`, `chibi_capabilities`,
`chibi_ecs` and `chibi_events`, which use `chibi_types`; 32 sources in nine
libraries, all of which compiled and archived from the graph.
Slice 4, 2026-10-21: the graph owns every library whose compiles include only
staged headers of libraries it owns: 31 libraries, 82 sources, including
`chibi_shader_runtime` (which, despite slice 3's note, needs no generated
header: its artifact-format record goes into `engine.json`), the native
capability tables, and three header-only libraries. Its target table,
`tools/graph/engine/Targets.chibi`, was first read from what CMake staged and
compiled; it is now the source of truth. The host's instruction budget rose
to 100 million and its step limit to 4,096 for graphs of this size.
Slice 5, 2026-10-21: the in-tree vendored C (`chibi_fmt`, `chibi_json`,
`chibi_asset_kdl`) and the libraries over it (`chibi_asset_json`,
`chibi_asset_text`, `chibi_text_capabilities`), plus `chibi_array_capabilities`,
`chibi_field_capabilities` and `chibi_input_map`: 40 libraries, 100 sources.
Each target now names its include directories, defines and options
explicitly, as CMake orders them.
Slice 6, 2026-10-21: generated sources. `build.embed_file` writes the byte
arrays `cmake/embed_binary.cmake` wrote, and the graph embeds
`chibi_builtin_fonts`' sixteen fonts before compiling: 41 libraries, 117
sources. A `--target` build stages the headers of the libraries its includes
name.
Slice 7, 2026-10-21: `chibi_component_presentation`, whose engine component
KDL is embedded, and `chibi_save_runtime`: 43 libraries, 122 sources, all of
which compiled and archived from the graph. Every other library now waits on
one of three inputs: `chibi_execution` (51 libraries include it; it compiles
SLJIT), Cute math (`public/math`, 46) and `chibi_native_types` (39), which the
reflection host tool generates. The first two come from fetched sources.
Slice 8, 2026-10-21: executables. The graph links `reflect` with CMake's
command, and the program it links writes the same reflected sources
(`build-graph-reflect`). A failure inside a called function now reaches the
build host's diagnostic, through the failure observer package tests use;
before, it read as "instruction budget exhausted".
Slice 9, 2026-10-21: profiles. The graph finds the Makefile's sanitizer,
debug or release profile on the machine: compiler, its kind, architecture,
SDK, ccache, libclang, `ar` and `ranlib`. From only the profile's name and
CMake's compiler as `CC`, its 129 commands equal CMake's.
Slice 10, 2026-10-21: the third-party archive (#569). The graph finds the
unpacked archive `cmake/third_party.cmake` loads for its profile and builds
`third-party/` sources and include directories from it: SLJIT,
`chibi_execution`, the header-only `chibi_math` (Cute's math headers, staged
and included as system headers), and the fourteen libraries that need only
those: 60 libraries and 218 compile commands, all of which compiled and
archived (or, for `reflect`, linked) from the graph.
`build-graph-archive` builds SLJIT and a math-header library, and
`build-graph-reflect` now reads the math headers the graph staged. Writing the
commands quotes arguments through the host's split, 7 s down to 2.4 s.
Slice 11, 2026-10-21: generated sources and per-source options. A target
whose sources `reflect` writes makes the graph build reflect, stage the math
headers and run it as `generate_runtime_registry` does, recorded like a link
(`c.Step`); `--reflect` runs another, as cross builds do. Generated sources
are `build/` paths, and a source can add options, as CMake's source property
does. The graph owns `chibi_native_types`, `chibi_native_bindings` and ten
libraries over them: 72 libraries and 250 compile commands, equal to CMake's,
whose reflected sources equal CMake's byte for byte.
Slice 12, 2026-10-21: tree-sitter and Box2D/Box3D from the archive. A target
names its C standard and position-independent code, and a source its own
include directories, which come before its target's. The graph owns
tree-sitter's runtime, the CSS and HTML grammars, Box2D, Box3D, the language
and project syntax documents and UI authoring: 79 libraries and 362 compile
commands, equal to CMake's. An up-to-date build of all of them read 6 MB of
records and ran out of the host's 32 MB of never-freed values; records now
keep each path and stamp on separate lines, and it takes 7 s. That leaves
about twice the headroom; the next slices about double the sources, so a
host-side stamp check or freeable build values must come with them.
Slice 13, 2026-10-21: C++ and shared private headers. The graph compiles
`.cpp` sources with the profile's C++ compiler (`CXX`, or the driver beside
the C compiler) and the target's C++ standard, and stages `private/NAME`
directories several libraries share. It owns enkiTS's task scheduler and the
twelve libraries it unblocks (task pool, material, physics, physics3d,
visibility, graph model, authoring and compilation, Studio catalog and graph,
engine and the default native catalog): 92 libraries and 484 compile
commands, equal to CMake's. The host's value storage rose to 256 MB for
up-to-date builds of graphs this size.
Slice 14, 2026-10-21: rewritten codecs and the language. The graph writes
Cute's codec headers with private symbols through a new `build.replace`
native (`core:strings.ReplaceAll` took 11 s), stages the language's private
headers below `sdk/language/private`, and owns image preparation, the
language, compiler, authoring, asset and model authoring, shader
preparation, source catalog and presentation, graph catalog and player
libraries: 103 libraries and 579 compile commands, equal to CMake's. A text
value now holds 16 MB, since `compile_commands.json` passed 1 MB; a step's
own output stays at 1 MB.
Slice 15, 2026-10-21: the desktop libraries. The graph compiles each
source as its language: C, C++ with the target's C++ standard (C++17 for the
UI host over RmlUi), or Objective-C (`-x objective-c`, CMake's `gnu2x`, and
no debug directory, which root CMakeLists.txt adds before Objective-C is
enabled). Export names that differ from a target's name are tables, and the
Vorbis decoder is rewritten like Cute's codecs. It owns the platform, model
import, UI host, renderer and project compilation, format and preparation
libraries: 110 libraries and 692 compile commands, equal to CMake's when
split as the shell splits them. `build-graph` now compares commands alone,
and `build-graph-support` holds the rebuild cases.
Slice 16, 2026-10-21: executables. The graph links the build host, the
compiler host and the `chibi` launcher beside `reflect`, with object
libraries, per-executable link options and link libraries in CMake's order,
and writes `link_commands.json`, whose four commands `build-graph` compares
with `build.ninja`: 116 targets and 711 compile commands, equal to CMake's.
Slice 17, 2026-10-21: C++-linked executables. On macOS the graph links the
player, `chibi-compile` and `chibi_ctl` with the C++ compiler, over the
compiler syntax and JSON libraries and the automation library, naming the
desktop archive's libraries and frameworks in CMake's order: 122 targets, 731
compile commands and seven link commands, equal to CMake's. Their Linux
links wait for a Linux archive.
Slice 18, 2026-10-21: the small test drivers. The graph builds and links
`test_host`, `concurrent_runner`, `policy_worker` and `benchmark_trials`,
which link no library, staging the host tests' headers below
`BUILD/testing/host/include`: 745 compile commands and eleven link commands
on macOS, equal to CMake's.
Slice 19, 2026-10-21: `test_language`. The graph configures files as CMake's
`configure_file` does, from templates whose placeholders take a file's bytes
(a new `build.byte_list` native) or its text as a C string, and gives a
source its own defines, with the source root, build type and a file's
SHA-256 filled in. On macOS it builds and links `test_language` over its
object library: 850 compile commands and twelve link commands, equal to
CMake's, and 28 generated headers equal to CMake's.
Slice 20, 2026-10-21: the SDK contract consumers. On macOS the graph
compiles the 21 object libraries `test/tools` declares over staged SDK
headers and the fixtures it copies beside pico_unit, and expands a target's
own defines: 949 compile commands, equal to CMake's.
Slice 21, 2026-10-21: `test_tools`. On macOS the graph links it over those
consumers and writes every executable where CMake does, so the paths it
compiles in for `chibi_ctl` and `policy_worker` (`{program:NAME}`) and the
compiler it exports C with (`export_compiler.h`) equal CMake's: 1061
compile commands and thirteen link commands. A build step now takes up to
1,023 arguments, which its 497-argument link needs; the graph-built
`test_tools` passes `make test-tools`' cases and `build-process`.
Slice 22, 2026-10-21: the build tree's install layout. Before compiling, the
graph writes the build identity (`engine.json`) as
`sdk/artifact_formats.cmake` does and installs the launcher's `share/chibi`
(identity, core and documentation sources, shaders) and `chibi_ctl`'s
resources: 213 files, equal to CMake's.
Slice 23, 2026-10-22: `test_language`'s runtime input. The graph writes its
`corpus.txt`, every fixture source by absolute path, equal to CMake's.
Slice 24, 2026-10-22: on macOS `make build`, `make build-debug` and `make
build-release` build with the graph, by profile name, as the owner chose on
2026-10-22; Linux, the SDK, web and Studio builds keep CMake. `Build.chibi
third-party` prepares the archives, a missing or older host is rebuilt with
CMake in `BUILD_DIR-cmake`, and `make graph-reference` configures that tree
for the build-graph checks. The host protocol rose to 7 for the natives the
graph uses. Initial local validation reported a 25-minute cold sanitizer build
and an 18-second no-op. The macOS CI graph build later reached the job's
45-minute limit before it finished.

The 2026-10-22 repair keeps dependency parsing in Chibi and scans the original
bytes, folding continuations without retaining whole-file native intermediates.
Large record and command-database joins use existing `build.join`; temporary
small path joins remain collectable. Invalid target names now fail before
profile discovery or writes. No new native capability or protocol change is
needed. A sanitizer test-driver rebuild passed in 247 s on a partially
built tree. The native, materials, projects, language and tooling groups passed
along with compile/link
command parity and the expanded target/parser regression. Current clean-build
and focused validation evidence is tracked in PR #655. Full native/render/Studio
coverage was not established by these focused results.

The 2026-10-22 build-program compiler cutover removes C semantic preparation
from the private build host. Protocol 8 loads an owned compiler generation from
the validated bootstrap and current compiler/core sources, then uses
`CompileClosureWithEffects` and the runtime-only native linker for Build and
Graph programs. Matching source hashes load directly; changed sources recover a
fixed point in memory without writing an SDK cache. Build-tree and installed
layouts carry the full compiler/bootstrap and core inputs, with explicit
checkout overrides for Make invocations. The stable twelve-argument pure
bootstrap exports remain; thirteen-argument effects-aware exports carry the
private catalog's real native effects through authored compilation.

Validation passed for actual Graph native compilation, 970 compile-command and
thirteen link-command parity, relocated read-only SDK loading, corrupt-seed
refusal, qualified native calls, inferred numeric locals and native effects.
The five required Make groups passed 1,561 cases and 1,568,825 assertions. An
isolated compiler/core-only recovery with an empty tool PATH reproduced the
refreshed 11,856,792-byte compiler, SHA-256
`2fcbdbaf6b999d87b119a8e6195752dc2b5aca18016e9349b9f6ae1a5bfa2b37`.
Publication also requires committed-source recovery and hosted cross-platform
validation. Public game compilation has not switched: StageOne's C catalog export
and assembly, declared components, libraries, systems and projections still need
their owning cutovers. Native build-host construction, SDK, other native-platform,
Wasm, Studio and parity/recovery CMake paths remain. This change does not complete
the final installed build/link and checked-game acceptance gate.

Next: packaging the SDK from the graph's build, the browser builds and Linux
(which needs a Linux archive the owner approves).

Move product profiles, generated prerequisites, native/cross compilation,
include/link ownership, install layouts, vendored dependency builds, and artifact
assembly into Chibi modules. Preserve installed CMake package files while Studio
or another supported consumer needs them; generate those files rather than using
them as the internal source of truth.

Native reflection remains a host tool during browser builds until its Chibi
replacement reproduces generated identities, layouts, bindings, and transactional
failure behavior. Complete M6 only after native, runtime-only, browser, installed
SDK, relocated SDK, and source-free consumer cases pass from the Chibi build.

### M7. Finish the stable command interface

Status: in progress. Every command is implemented and covered; only moving
`export` behind Chibi modules remains, and that is M6's build-graph work.
`check` runs the build's compiler with `--check`, which prepares the project
read-only: it neither migrates an older project nor writes an artifact. The `chibi-cli` fixture covers it in the build tree, through the
launcher with a fake compiler and through the real `chibi-compile`;
`chibi-cli-installed` covers the installed SDK and
`commands_fail_without_the_compiler` a missing compiler. `version` prints the SDK,
build, compiler and artifact-format identity from one generated document,
`share/chibi/engine.json`: configuring writes it, build trees keep it beside
their shaders, and SDK packaging installs it with the build ID. The document
also records the platform (every processor of a universal build) and
execution backends the SDK was built for. `doctor` reports the SDK, compiler,
player, platform, native backend and, with `--web PLAYER`, browser export as
supported, unavailable or broken, and fails only for a broken one it checked;
it never writes. A platform mismatch is reported as unavailable, because the
compiler and player checks decide whether the SDK runs. For `test`, the owner
chose an attribute contract on 2026-10-19 after a survey of Rust, Odin, Jai,
Zig, Go and others: `@test` marks a package-level function with no parameters
or results, and the core function `expect(condition, message)` fails it at the
call's position (see `docs/code-language.md`). `chibi test [ROOT]` hands ROOT
to the compiler's `--test`, which prepares every package below it with the
SDK's `core:` collection (build trees stage it in `share/chibi/core`) and the
natives that need no running game, runs each test alone on the default backend
for at most 100 million instructions, and prints each outcome, where a failure
stopped and a count. A test that waits or starts a task fails. ROOT holds only
package files, so a game project's behaviors are not tests yet.
`src/authoring/package_tests.c` finds and runs the tests; `chibi-cli` covers
the launcher, the real compiler and a compiler without the core collection.
Since 2026-10-20 it also links the compiler's raw-syntax and JSON bridges, so
the compiler's own packages can hold tests (`chibi-cli`'s
`compiler_test_links_the_compiler_bridges`). `chibi fmt [--check] [PATH...]` hands
each project to the compiler's `--format`, which runs the formatter that moved
out of the automation controller into `src/authoring/project_format.c`;
`chibi_ctl --format` now calls the same module. A PATH that is a folder
without a `project.kdl` is a package tree instead: the compiler formats each
`.chibi` file below it that `chibi test` would read, after PR #478 taught the
source formatter package syntax. Every file must declare a package, checked
before anything is written, so a project's behaviors are never formatted
without the metadata only project formatting keeps. `packages/` and
`tools/docs` are canonical, and so is `tools/compiler` since the M5 work
formats it with every change; the `chibi-cli` case
`repository_packages_are_canonical` checks all three with the real compiler. `chibi web [PROJECT] --player PLAYER [--port PORT]`
exports the browser site into a `mkdtemp` folder under
`TMPDIR`, serves it on 127.0.0.1 (port 8000 by default, 0 for any) with the
launcher's own static server, `src/host/serve.c`, until SIGINT, SIGTERM or
SIGHUP, and then removes that folder; a busy port or failed export leaves
nothing behind. The server answers only requests whose Host is this computer,
against DNS rebinding, and gives each connection ten seconds. Like a static
host it sends no cross-origin isolation headers: with them the non-threaded
browser player aborts in WebGPU. Windows does not serve yet.

Coverage: `chibi-cli`, in `make test-projects` against the Release build, runs
each command against the build tree, a fake compiler and player, and a fake
browser player, including help, exit status, failure
paths and missing capabilities (no compiler, no core collection, no browser
player, a busy port). `chibi-cli-installed` runs the packaged SDK's own
launcher on `version`, `doctor`, `check`, `fmt`, `build`, a native `export` and
`test`; `run` and `web` stay with the build-tree cases, which start no player
window. The unnamed run alias is gone. Moving `export` behind Chibi modules is
M6's build-graph work, and `chibi doc` arrives with M8.

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

The unnamed run alias is removed (see "Stable command interface"). Every command
needs help, exit-status, installed-SDK, build-tree, missing-capability, and
failure-path coverage.

### M8. Generate and publish one documentation site

Status: implemented, reviewed and published. The site has been live at
https://waldnercharles.github.io/chibi/ since 2026-10-20, as the end of this
status explains. `chibi doc [ROOT] [--serve] [--port
PORT]` hands ROOT to the compiler's `--docs`, which writes `ROOT/build/docs` from the manifest
`ROOT/docs/site.kdl`: a title and sections of pages, each a Markdown source
inside ROOT and an `.html` file name. Nothing else under `docs/` is read. The
generator is authored Chibi in `tools/docs`, which SDKs ship as
`share/chibi/docs`: `markdown` renders the subset the public pages use
(headings with GitHub anchors, paragraphs, lists, block quotes, fenced code,
pipe tables, inline code, emphasis and links) and maps links between published
sources to their pages, dropping links to anything unpublished and reporting
each one at its source line; `site` lays out
the navigation and stylesheet. The C side reads the manifest and files and
calls the generator on the natives that need no running game. A page's source
must resolve, links included, to a regular file inside ROOT. The site is written
into a fresh folder that then replaces `build/docs`, so the published folder
never holds a partial site or a page the manifest no longer lists. `--serve`
serves the result like `chibi web`. Examples are checked: every block fenced as
`chibi` on a published Markdown page must parse, and one that is a whole package must
analyze against the natives a test may call, or the build fails at the
example's line. A page can also be generated: `packages` with its
`collection` lists every package folder's declarations, signatures and the
comments above them (the generator's `reference` package), and `grammar`
lists each rule of a Tree-sitter grammar's `rules` block with its comments
(`grammar`). The site now has all eight sections: the README (Overview), the
language reference (Learn), the Testing guide, the generated `core` reference
(Packages), the examples, the FAQ, the generated Grammar and the Toolchain
guide. The manual `Documentation site` workflow
(`.github/workflows/docs.yml`) builds the site, uploads only `build/docs` as
the Pages artifact and, since the owner approved it on 2026-10-19, deploys it
with `actions/deploy-pages` (one deployment at a time, never cancelled). A
pre-review of the artifact on 2026-10-19 found no local paths,
credentials or links into private repositories (the linked `chibi-studio` is
public). At the owner's request the README, Examples and Learn pages were then
rewritten so that no sentence leans on a link to an unpublished document (the
site had dropped 25 such links to plain text), and Examples no longer names the
archived private repositories. `chibi doc` now prints `SOURCE:LINE: link to
TARGET is not published; written as text` for each link it drops, comments in
generated pages included, so a page cannot quietly regain one; the
repository's own site reports none. The site folder now gets the permissions
`mkdir` would give it rather than `mkdtemp`'s 0700, so a static host can read
it. At the owner's request the repository's Pages source became GitHub Actions
on 2026-10-19; the 2026-09-29 browser build from the retired engine publisher
on `gh-pages` keeps serving until the first deployment replaces it. GitHub
runs a `workflow_dispatch` workflow only from the default branch, and the
`github-pages` environment deploys only from `main`, `gh-pages` and `v0.1.0`,
so the owner's instruction was to run the workflow once this branch reached
`main`. It landed on 2026-10-20, but GitHub refused to start the workflow's
job because hosted execution was unavailable. At the owner's direction the site was instead built locally from
`main` (`5a8457bf9`) with `build/sdk/chibi doc`, committed to `gh-pages`, and
the Pages source switched back to that branch. The commit, `0b17fddde`,
replaces the old browser build, whose editor now lives on chibi-studio's own
Pages site. To publish again, build the site from `main`, commit `build/docs`
with `.nojekyll` and a `BUILD_COMMIT` naming the source revision to
`gh-pages`, and push. Returning to the workflow means switching the Pages
source back to GitHub Actions once hosted execution is available. No published page uses an
image yet, so the manifest has no
static assets beyond the generated stylesheet; listing assets comes with the
first page that needs one.

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

Status: in progress. The retired web publisher is gone (2026-10-19):
`tools/publish_web.py`, `tools/package_web.py`, their `test/tools` cases and
the disabled `make publish-web` stub. Its only callers were those tests and the
stub; no workflow, SDK install, release or Studio path called it. Its
publication contracts already live in the replacement: Studio's
`publish.yml` records both repositories' commits and the editor archive's
SHA-256 in `studio.json` and skips an unchanged pair, the engine release
workflow publishes versioned packages, `docs/game-web-workflow.yml` deploys a
game from them, and `chibi export --web` assembles a local site. Git history
is the recovery path.

On 2026-10-19 the owner asked to start removing project-owned C, C++, Python,
CMake, Make and shell wherever Chibi can own the work, and to do no work in C
or stage zero that Chibi can do. The removal inventory in
`docs/toolchain-inventory.md` now classifies every such file. The in-tree
browser editor's leftovers are gone: `tools/configure_web.sh`,
`tools/web_compiler.sh`, the root `CHIBI_COLLAB_URL` and the copies of
`tools/web_*` and the collaboration client. Nothing had called them since
Studio took over the browser build on 2026-10-08 (`aff61c28f`); Studio builds
from its own `web/` and `test/tools/web_storage.c` already reads that copy.
So are the retired one-off probes: `tools/hrc_sparse_sweep.c`, built by
nothing since its September HRC study (its research note says how to recover
it from Git), and `tools/compare_neon_frames.mjs` with `tools/png-check.mjs`,
a frame diff for a game no longer in this repository. `Build.chibi` now decides
whether private dependency sources decline immutable packages, which was
`configure_inputs.py package-enabled`; the `build-toolchain` cases passed
unchanged against both implementations. Compiler SDK discovery followed it
from `configure_inputs.py compiler-sdk`; the new `build-toolchain` macOS case
passed against both implementations. A step's own output now holds 1 MiB
rather than 4 KiB, probes keep tool reports out of the build's output, and the
build host gained the file primitives in `docs/self-hosting-build.md`.
Dependency acquisition is replaced (2026-10-20): the owner chose prebuilt
libraries, as Odin's `vendor/` keeps them, published as GitHub release assets
instead of committed. `third_party/archives.tsv` pins one archive per target
and variant (release, ASan+UBSan, ThreadSanitizer) by SHA-256; `Build.chibi`
unpacks it into the clone's `.git/chibi/third_party`, which its worktrees
share. The SDK links the archive's libraries, including the small ones it
exports as its own targets (Box2D, Box3D, SLJIT, enkiTS, tree-sitter and its
grammars), and installs copies of them. The FetchContent declarations, configure-time
patching, `dependency_package.py`, `fetchcontent_cache.py`, the package
decision `Build.chibi` took over from `configure_inputs.py package-enabled`,
their tests and CI cache keys are deleted, as are the engine's unused Zep copy
and its never-linked Tracy option. `third_party/CMakeLists.txt` builds the
archives in CI and `cmake/third_party.cmake` can download one on the CMake-only
recovery path; both are temporary and leave with CMake. Windows CI is paused
until the build host runs there. The configuration signature can move next.
On 2026-10-20 the owner approved three deletions with no replacement, now
done: the hosted M5 validation helpers in `.github/scripts/` with their
`native-unit-dependencies.tsv`, which no workflow ran; the CUT61 `analyze.py`;
and game runtime installation (`make update-game-runtime`,
`tools/install_game_runtime.py`
and `tools/kdl_asset.py`), which served the retired game repositories. Git
history keeps them. The CUT61 `run.py.txt` runner and the Studio UX review's
`browser-sample.js` sampler followed the same day, also approved.
On 2026-10-20 build programs became packages: one file with a `package`
header that may import `core:`, with `format_text` and the array natives
(host protocol 5). Their `build.` string natives go once the programs use
`core:strings`.
On 2026-10-21 the owner retired the mesh-to-SDF converter (`tools/mesh_sdf/`
and its `box.obj` fixture) with no replacement, rather than port its Blender
launcher; its worker required Blender 4.3.2 on Linux, and its tests and the
example that used it were already gone.
Everything else is blocked. Packaging still needs sizes, modification-time
order, link reads and staging directories from the host.
The build graph waits on M6, the executables and runtime on a native backend,
and engine modules on M9.

Delete one responsibility at a time. Before deletion, name the replacement,
enumerate every repository, installed-SDK, CI, release, and application caller,
and run the equivalence checks in `docs/toolchain-inventory.md`. Remove obsolete
wrappers and docs in the same change. A repository-local `.chibi` script does not
replace an installed tool until the installed SDK works without engine source.

Python may remain only where an external application requires its Python API,
such as Blender integration. Hosted workflow YAML, native backends, explicit
platform assemblers/linkers, SDKs and runtime/operating-system libraries may
remain dependencies behind narrow bindings. This does not exempt `cmake`,
`clang`, `gcc` or hidden/renamed equivalents from the final normal-workflow ban
in the Objective. External C compilers remain stage-zero construction/recovery
tools during migration, not normal installed build dependencies. All Chibi
project policy called by allowed dependencies belongs in `.chibi`.

## Current checkpoint

Graph compiler-tools work is in progress on
`codex/graph-sdk-packaging-20261022`, based on main `5c28191b3e`. The patch is
validated on macOS: SDK 8 cases / 205 assertions, host 44 / 7,005,
toolchain 11 / 165, graph 3 / 1,004, all five required groups and formatting.
Three guard-removal controls each fail the intended case. Full execution used
tree `77493ea3dbf675f3640b607b00cd16ed924ee9c1`; a diagnostic-only whitespace
refresh used `84ca3290f833dafb4ad1bc3605700c4698c30791` and reran the affected
host check plus formatting. Final ledger updates leave that tested product and
test source unchanged.
Next action: run the explicit Linux graph/relocation recipe on the eligible
combined dependency candidate. Full SDK libraries/headers/exports, installed
StageOne execution and Windows, browser and Studio acceptance remain open.
See the M6 packaging slice above.

### Prior compiler checkpoint

Last verified checkpoint: 2026-10-19, the #420 integration of `main` at
`07eb2abaa` into `codex/toolchain-cleanup-20261017` at `f6633ace1`, verified
locally on macOS arm64 (Homebrew Clang 23, Debug, ASan/UBSan; Release for the
bootstrap checks). Each command ran once:

| Check | Result |
| --- | --- |
| `make test-native`, `test-materials`, `test-projects`, `test-language-all`, `test-tools` | Passed; 27.4s, 29.1s, 30.2s, 29.2s and 25.8s. |
| `make test-wasm`, `check-format` (clang-format 22) | Passed; 4.4s. |
| 87 standalone language and tool selectors | Passed. |
| `language-package-compiler-chibi`, `-types`, `-imports` | Passed; 31.5s, 27.0s and 43.2s. |
| `make test-bootstrap`, `bootstrap-artifact`, `test-recovery` | Passed on the committed merge; 24.9s, 24.9s and 10.6s. |

The merge found and fixed a record copy that left field type names and units
in the source arena, runtime array copies that scanned every source element for
packing (quadratic for slices of large arrays), a formatter that could not read
prototype links, and compile-time conversions missing two runtime checks. The `imports` sample moved
to its own selector because it compiles all 605 lines of `core:strings`.
The integration landed on `main` as #516 (`a9dc954bd`) on 2026-10-20, and
#512 rebuilt the checked-in bootstrap artifact from it. M5 step 4 followed
`docs/design-notes.md`, gating each stage-one feature on a purpose-made
story; stage one rebuilds all five as stage zero does since 2026-10-20.
Since 2026-10-20 stage one's functions also have all of stage zero's
debugger points (function, statement, completion, expression and symbol) and
watches (`docs/package-compiler.md#output`). The bootstrap compiles the
compiler without them, since with watches stage two outgrew the compiler
host's bounds. Also on 2026-10-20:
- #586: stage one binds handler parameters to payload fields by name, in any
  order or subset, as format 30 does.
- #588 and #592: stage one compiles maps.
- #591: stage one compiles `ref` parameters of locals.

All three were checked on the Wasm host too.

Still open:
- Stage one doesn't check a handler parameter's written type or unit against
  its field. Its game input carries no field units.
- Stage one compiles `assert` wherever the target links the core pack, as a
  game does; `Target` carries its failure kind. The compiler host links no
  core pack, so neither stage compiles `assert` in the compiler's own
  packages.

Next action: the bootstrap sequence on every supported host configuration.

Host status on 2026-10-20:
- macOS arm64 runs the whole sequence locally.
- On the 32-bit Wasm host, stage one compiles every sample and behavior, and
  the closures run there and match stage zero's points and watches
  (`wasm-package-compiler-chibi*`, `docs/package-compiler.md#hosts`).
- The bootstrap itself stays native: stage one compiling itself outgrows
  Wasm's 2 GiB linear memory.
- Linux x86_64 and Windows x64 are deferred (owner decision, 2026-10-20).
  On 2026-10-21 the owner counted M5 complete for macOS arm64 and Wasm and
  readied M6, with these runs as M5's open gate (see M6).
  - They run when CI is back (disabled since 2026-09-25) or when
    a bigger host is available.
  - Until then, stage one stays off as the normal compiler, and stage zero
    remains the normal compiler.
- Local attempt on 2026-10-20, an Ubuntu 24.04 container under Rancher
  Desktop:
  - Linux x86_64 under Rosetta built the Release `test_language` and
    `chibi-compiler-host` from main in 13.5 min.
  - The bootstrap check and the Debug build were killed for memory. The 6 GB
    VM is shared with other containers, and the check peaks at about 5 GB.
  - Linux aarch64 doesn't build, because Cute's build passes `-msse4.1`.
  - CMake 4 also needs `clang-scan-deps` (`clang-tools-18`), which CI's
    package list lacks.

Prior toolchain checkpoint: 2026-10-18, PR #445 transport integration
over `c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb` (PR #444), verified locally.

| Check on the macOS arm64 candidate (Homebrew Clang 23, Debug, ASan/UBSan) | Result |
| --- | --- |
| `make build` of `chibi_ctl`, both drivers and `chibi-compiler-host` | Passed; build timing is separate. |
| `language-package-snapshot-chibi` | 16 cases / 248,742 assertions, 2.7s. |
| `compiler-artifact` | 15 cases / 29,562 assertions, 4.2s. |
| Union, effects, record, unit-analysis and type-relations differentials | 3, 4, 5, 3 and 4 cases; 17.0s, 3.2s, 2.2s, 4.6s and 0.7s. |
| `language-execution` | 23 cases, including the wide-suspension budget case. |
| `make test-native`, `test-materials`, `test-projects`, `test-language-all`, `test-tools` | Passed; 23.9s, 28.8s, 31.2s, 18.4s and 27.1s execution. |
| Host symbols (`nm -a`) | No `execution_prepared_write`, `semantic_*`, `lower_*` or `authoring_packages_prepare`. |
| Changed C, clang-format 22; `git diff --check` | Passed. |

Each selector ran once.
The hosted Linux ELF audit (`.github/scripts/transport-boundary-audit.py`) was not
run; its archive closure and all 19 pinned source inventories match this build's
File API/compile database, and the writer exclusion holds on Darwin's dead-strip
link. Linux remains unmeasured for this checkpoint.

The unit-analysis differential and its two independent regression selectors are
wired. Baseline source `8b2e2116bdaa3270d347ab854970ae3bb6591393` passed
preparation and built in Studio run `39112128113` (configure 3s, build 17s).
For both bytecode and SLJIT attempts, the independent C witnesses passed before
the authored calls exposed the intended zero-ID mismatch and recursive
callable-depth failure. Each focused
selector completed in about 1s. Verified sanitized status evidence SHA256:
`d4ec88911f4523f3231d0332fb12cb03e97c1e9386c5cb411fc58dc15c647313`.
The expected-red job remains failed; this records the regression, not a pass.

The focused deep diagnostic alone captures the first failure before child frames
retire. That opt-in disables readonly argument borrowing and uses snapshots.
Zero-ID and ordinary/full differential calls retain the original no-observer
path. No production runtime propagation, evaluator policy or backend selection
was changed to collect the evidence. Raw diagnostics stay runner-local.

The merged authored-only repair removes the invalid zero-capability guard and
replaces recursive Eval calls with explicit work/operand stacks. It preserves
cache visits, selective dependency evaluation, diagnostic order and unknown-unit
behavior. The normal production analyzer is unchanged. Source
`ce379205fc529f8d5d2142bc6346e0079c7ec827` passed the complete three-case
differential in hosted run `39112565011`: 25 raw and four analyzed requests,
58 authored attempts across both backends, and owned results surviving
input/package teardown. Configuration took 5s, build 14s and the single
production-supervised execution 2s. Its sanitized evidence hash is recorded
above; the merged tree exactly matches that tested source tree. Instruction-budget arguments
remain unchanged. The supplied instruction
budget is inherited/reset per activation, not a whole-call work ceiling.

The independently reviewed additive type-relations component passed its first
complete differential in hosted run `39113816903`. Its four cases cover 105 fixed
requests, 210 authored calls across bytecode/SLJIT, ten preparation parses and
retained-result lifetime checks. Configuration took 1s and build 14s; execution
completed below the one-second timing resolution. This standalone Debug build
was not sanitizer-instrumented. Exact source, merge tree and sanitized evidence
are recorded in the milestone above.

Union members, effects and record members are merged additive fixture components.
The record preparation fix and Effects local-name correction passed the complete
five-selector semantic run recorded above. That pass does not validate this new
transport integration: it must rebuild, audit and run all seven complete selectors.
Existing backend/lifetime assertions, instruction arguments and individual
deadlines remain unchanged. No active adapters or native-unit results are imported.

`semantic_units.c` now uses the extracted C reference helper (PR #443). It
supplies raw analyzer facts, copies returned diagnostics into the analyzer's
ownership before freeing temporary storage, and retains the existing position
sort. The authored source and evaluator are unchanged; this is the production C
extraction boundary, not production dispatch of Chibi analysis. A C-only
standalone tooling parent that consumes scheduler/render C++ targets now
activates CXX through a narrow production include, leaving pure C consumers
alone. On macOS, `language-semantic` (42 cases), `language-incremental` (13),
`unit-fixtures` (2), `language-unit-analysis-chibi` (3) and the eight-case
`desktop-dependencies` route pass, as do the five CI aggregates. The desktop OpenGL case previously
expected Linux relocation on every host; it now asserts that other hosts reject
the same absolute paths as unowned. The earlier hosted Linux build failure was
never classified and Linux remains unmeasured for this adapter.

Numeric conversions are now spelled `T(value)`, replacing `cast<T>(value)`,
because the bounded compiler sources need explicit narrowing. Type names resolve
to conversions before catalog capabilities, so the Byte literal node is now
spelled `byte_value`. Mixed signed/unsigned integers compute and compare in
`int` or `int64`. Integers widen implicitly only when lossless; `T(value)` is checked (a constant that does not
fit is a compile error, a runtime value fails the invocation), `T(wrap: value)`
keeps the low bits and `T(clamp: value)` saturates. `BF_CAST` carries the mode in
every backend, prepared format 15 records it, and dead-code elimination keeps a
checked cast whose value is unused. Typed constants use `Name: T : value`.
Project format 25 migrates `cast<T>` sources and their sidecars; Studio's
`projects/studio` uses `cast<` and upgrades when opened. The authored scalar
folder mirrors the mode matrix. Unsigned ordered comparison and subtraction are
still unsupported by the verifier and backends. On macOS the five CI aggregates,
the Wasm group and 40 focused language and tool selectors pass; Linux is unmeasured.

The bounded authored package compiler (PR #449,
`tools/compiler/bounded_compiler`) now runs. Stage zero prepares its 18-file
closure, and the authored `Compile` entry turns one-function source packages
into CHIBICLO artifacts. Those artifacts run on bytecode and SLJIT. On macOS,
`language-bounded-compiler-chibi` passes 43 sources on both compiler backends,
with 28 accepted payloads and 56 target runs. The expression subset is
deliberately small (see `docs/bounded-authored-compiler.md`). Since PR #454 the
compiler also writes its own CHIBIPRE module and CHIBICLO closure through the
authored prepared writer, so only the raw parser is native after bootstrap.
The runtime verifies and finalizes the unoptimized definition on load; the
target supplies its header ABI and instruction ordinals. The native scalar
finalizer is gone. One scalar function is not stage one or stage two.

Self-hosting needs arrays that grow in linear time, and they did not. Every
`array_add` copied the whole array and every element write snapshotted the
root, so 16,000 appends took 14 s and 8,000 record appends 28 s. The 27
compiler packages compile to 311 functions and 236,000 instructions, which
stage one must build. Lowering now emits in-place `array_push`,
`array_extend` and `array_store` intrinsics for a local or result array, which owns its items
because every store copies. Capacity doubles like Odin's dynamic arrays, and
16,000 record appends take 0.07 s on SLJIT. Parameters still alias the caller's
array and keep the copying path. The counted-row builder this replaces is gone.
Collectable arenas, which program activations use, still walked every live
value on each record copy. They now index live values by page, so 40,000
collectable record appends take 0.67 s (PR #452).

The authored prepared writer (`tools/compiler/prepared_writer`, see
`docs/authored-prepared-writer.md`) reproduces `execution_prepared_write` byte
for byte. `language-prepared-writer-chibi` marshals five stage-zero modules,
including the writer's own 1.3 MiB module, into its flat model and compares
every byte on both compiler backends. Its `Fingerprint` and `Closure` match the
native fingerprint and CHIBICLO writer. A call copies record-array arguments
and checks every item of a borrowed scalar array, so the writer keeps its
tables in one function; stage-one code must do the same until borrowing covers
records. The bounded compiler is its first caller.

That rule no longer binds stage-one code. Calls copied every argument, so a
helper that read one row of a 4,000-row table cost 11 s for 4,000 calls. A
callee that leaves the caller's storage untouched now receives its arguments
without copies (see "Read-only plain-array traversals" in
`docs/code-language.md`); the same loop takes 0.1 s. Reads through a `ref`
parameter still copy the referenced value.

The stage-one compiler starts as `tools/compiler/package_compiler` (see
`docs/package-compiler.md`). It compiles packages of functions, records,
unions and `int` literal constants, and the `project:` and `core:` packages
they import, with `int`, `bool` and `string` values and arrays of them,
statements, control flow including `foreach`, exhaustive `switch` over unions
and `is` patterns with bindings, calls within and across packages, and calls
of the natives the target links, to runnable multi-module closures that agree
with stage zero, with no native step after the parser. Records and arrays use
stage zero's value model: construct, snapshot and in-place array intrinsics,
stage zero's record IDs and layout hashes, and value roots for suspended
callers. Unions use the union-make, -test and -get intrinsics with stage zero's
tags. Each package is a module whose import rows carry the fingerprints the
runtime checks. Integers are `int`, `byte`, `int64` and `uint64`, with literals
typed by their use and checked, wrapping and clamping conversions; the
compiler's own `x := -1` locals now declare `int`, since stage zero makes an
untyped literal a double. Array literals with no type expected take their
first item's type, variadic natives such as `format_text` and natives with
several results such as `compiler_json_string` work, and field assignment
writes through stage zero's reference intrinsics, with written record and
array parameters copied on entry. In-place array updates are recognized by
capability identity, as in stage zero. With these, stage one compiles the
compiler's complete source set, and the stage-two closure it builds, loaded by
the compiler host, compiles the same sources to the same bytes
and every sample as stage one does (`make test-bootstrap`, a Release build,
about 30 s). The compiler host allows a
compiler-sized closure: 256 MiB of decoded storage per load, 256 Mi units of
record-layout work per module, and a 16 MiB `byte[]` result. The bootstrap run's 5 GB peak footprint is
mostly stage zero's build writing the package compiler's module; stage two
alone compiles the compiler in 7.9 s with a 3.6 GB peak. Still open in this
milestone: a complete stage one that rebuilds a checked game (step 4; Terrain
Lab's behaviors need behaviors, events, state, doubles, vectors, named
arguments and results, and effectful natives).

Step 4's gate exists since 2026-10-19, and stage one passes it since
2026-10-20. At
the owner's direction (2026-10-20) it gates on purpose-made stories in
`test/stories`, one feature each, rather than on a playable game; playable
examples such as Terrain Lab are to live outside the engine repository. The
`game-equivalence` selector rebuilds each story with a reference and a
candidate compiler, each a program that takes `--project-root ROOT --output
FILE`. It then compares the two artifacts of each story:
- what they declare apart from their code: every byte outside the module
  records, and each module's name and ID;
- one observation per fixed step while both play the story's scenario on the
  CPU.

`test/tools/GameEquivalence.chibi` orchestrates and compares in the build
host. C reads and plays the artifact. Both compilers are stage zero until
stage one compiles behaviors. Stage one then needs a route that puts its
modules into a game artifact. Project preparation is now split for it:
`authoring_project_assemble` writes the artifact from the project's
declarations, scenes, prototypes and resources plus supplied prepared
modules, one per executable source by module ID. Assembling each story from
its own artifact's modules reproduces that artifact byte for byte. `chibi-compile --stage-one-input FILE` exports stage one's input for a
project: its executable sources, Compile's six target arguments, then the
catalog's events, the type requirements stage zero lists, the entity type,
and the natives that combine records for binary operators. Stage
one's `CompileBehavior` compiles Counter's behavior asset: retained state, an
event handler, `self` and a component field write through the reference
intrinsics, with the writer's state, event and component tables. It compiles
Motion's drift, `self.Transform += self.Velocity * dt`: component reads
through references, records combined by the game's `*` and `+` natives, and
the sum written back through the reference it read. It compiles Controls'
pilot: a field's compound assignment through its reference, `float`
arithmetic, a record field inside a game record, and `input.Jump.pressed`,
an input action's name read by the game's native. It compiles Results' fold:
member procedures, named results and conversions between integers and
floats. It compiles Follow's lead and trail: a floating state, natives chosen
among overloads by argument type, `vec2` as the record it makes, a record
written over a whole component, `globals.Leader` read by key, and the
leader's component. Its modules have stage
zero's identities and play as stage zero's do
(`docs/package-compiler.md#behaviors`). Stage one's route,
`tools/toolchain/StageOne.chibi`, rebuilds every story as stage zero does: it
recovers stage one from the bootstrap artifact, runs
`CompileBehaviorsWithCatalog` on the exported input, and assembles the
artifact with `chibi-compile --modules` (`make test-stage-one`). Stage one passes step 4's gate since
2026-10-20, and its functions have all of stage zero's debugger points and
watches. Next: the sequence on Linux x86_64 and Windows x64, deferred until
CI is back or a bigger host is available (2026-10-20), before
stage one becomes the normal compiler. See `docs/testing.md#game-equivalence`.

Two runtime gaps surfaced on the way. Argument sharing rejected every function
that assigned a field of its own record, because the analysis ignored the
reference kind stage zero keeps in a data cell's initial value, and because
storing the caller's text tainted the record. The analysis now reads initial
values and tracks stored text separately, tainting only a read of it as an
address, so 62 of the compiler's 63 functions share their tables. Compiling is
still slow, about 0.9 s for a 50-line sample on SLJIT in a Debug + ASan build.
Call retirement no longer re-measures every spare frame, a running stack reuses
frames larger than the 64 KiB idle budget, and descriptor copies no longer
re-derive layouts.

Compiler speed, measured on 2026-10-20 in Release on macOS arm64 with SLJIT
(`--package-compiler-chibi-bootstrap`): stage zero prepares the compiler in
2.7 s. Stage one compiled the compiler in 23.7 s and stage two in 16.9 s, about
70% of it re-validating immutable record descriptors on every value copy.
Records are now validated where they enter (creation, prepared loading, value
documents), so stage one takes about 7.6 s and stage two 6.5 s; a row copy in
`--script-benchmark` fell from 1,050 to 110 ns. Stage one then copied record
descriptors 31 million times (33 GB), 92% of them into an arena that already
held a copy; each arena now remembers its last copy, which removes half of
them and about 3% of the bootstrap selector's CPU time. The remaining copy cost is
emitted code: snapshots that retain locals and fields, and union constructors
that copy every payload array (about a third of stage one's samples). Cutting
it needs the emitter to move values at their last use rather than copy them,
in `tools/compiler`. Resetting each call's data values costs about 10%: 16
million calls reset 648 million values, of which 29% had changed, and zero
4.7 GB of buffers. A static write set (addresses taken, host and call results)
still covers 83% of those values, because programs fill argument banks through
their addresses, and saved no time. Marking writes at run time (a byte per
value, set by every address taken) restored half as many values but ran
slightly slower than main, so the resets stay as they are. LLVM AOT would not help this workload: code SLJIT generates
is 6.2% of stage one's samples, and the rest is the C runtime it calls, which
an LLVM backend would call too.

Two later changes cut more (2026-10-21). The emitter moves a fresh literal or
authored call result into a local or return instead of copying it (#613), and
the compiler hashes and validates source text by reading `Bytes` once instead
of a `ByteAt` per byte, which measured the whole text each time: 55 GB of
`strlen` per self-compile (#617). Stage one now takes about 6.5 s and stage
two about 4.6 s, against stage zero's 2.7 s. What remains, in the self-hosted
compiler's own run:

- Deep copies of rows: `Step.Emitted`'s 13 payload arrays per step (0.57 GB),
  read-only element copies such as `node := forest[i]` and `for (item in
  code)` (0.9 GB), and scalar arrays. Last-use moves and read-only aliasing
  need `EmitBody`, which is 41 KB under the definition verifier's 64 MiB
  budget (36,292 instructions × 14,750 slots), so it must be split first.
- The descriptor cutover resolves repeated source-length scans in
  `Text(forest, text, node)`. Slice still copies its selected bytes; shared
  backing remains a separate ownership optimization.

Later on 2026-10-21, `EmitBody` was split (#622), loops read unchanging arrays
in place (#627), and arrays change through methods (#628, #630). Recovering the
compiler from the bootstrap artifact, the self-hosted compiler's own run, took
about 4.4 s of CPU. Two runtime and emitter changes followed:
- Owned scalar arrays stay packed while they change in place (#635), so the
  prepared writer's byte arrays cost a byte per byte rather than a
  `pin_value`. This brought recovery to 3.6 s.
- A returned variant is now a fresh value (#637). This brought recovery to
  about 3.3 s, or 57 billion instructions.
- A function now builds what it returns in its caller's storage: returned
  literals and variants, and the array locals it returns (result locations).
  This brought recovery to about 55.0 billion instructions. 98% of results
  already lay wholly in the callee's storage, but callees allocate about 13
  times their result's size, so handing the callee's storage to the caller
  instead was ruled out. A result moves only when the argument-sharing
  analysis shows nothing but the updates that run in the caller's storage
  changes what the function builds there.

What remains, in that run's samples:
- The call-result copy into the caller is about 12%.
- Record and array constructors are about 16%. Their per-field layout checks
  are about a third of that.
- Resetting each call's data is about 10%.
- The JIT's own snapshots are about 5%.

The following evidence belongs to the unchanged prior compiler checkpoint:

- Repository: `chibi`
- Branch: `codex/toolchain-cleanup-20261017`
- Starting checkpoint: `a6624c47c95ef7d9cbf55559620141e64a3dafdc`, annotation reader pushed
- Integrated upstream base: `origin/main` at `6ea2221c97d266c9bb028443cb67bd445fdb3476`
- Latest fetch: upstream unchanged and already an ancestor
- Completed milestones: M0 through M4; M5 is in progress and M7 is ready
- Next action: wire and run the unit-analysis failing regressions, then fix its evaluator

`callable_headers.c` extracts registration policy from `semantic_declaration.c`.
Active wrappers provide raw live tables and syntax, apply masked symbol changes,
rebuild function membership vectors and queue original initializer/body nodes.
Existing symbol flags and deferred count finalization are preserved. Returned
names, units and memberships are owned; the adapter transfers them to the
analyzer's existing heap ownership. Obsolete standalone parameter APIs were removed.

`tools/compiler/callable_headers/Headers.chibi` owns the same sequential policy,
including generic bounds, duplicate/origin guards, implicit results, synthetic
entry symbols and macro entries/exits. Fifteen raw declarations compare full
changes, final tables, pending syntax references and refreshed reader views on
both backends. Explicit integer literals fix bootstrap generic-array inference.

A fixed 65-array annotation exposed a reader failure beyond the original shallow
corpus. Runtime limits apply to total depth at each callee, so `depth_limit 0`
on a recursive entry does not exempt default-limited helpers. Reader now uses
explicit task/wrapper/union continuations and an ordered member stack; diagnostic,
use and publication order stays intact while helper depth stays constant. The
same deep case failed the recursive source and passes the iterative source.
The bootstrap also requires an explicit final return after the infinite loop.

| Check on the macOS candidate | Result |
| --- | --- |
| Authored `make build`, language/tooling/both private hosts | Passed after bootstrap syntax corrections, 6.2s. Build timing is separate. |
| Annotation/signature reader differential | 4 cases / 128,443 assertions, 8.018s, including the previously failing deep case. |
| Callable header differential | 2 cases / 89,805 assertions, 5.668s. |
| `make test-native` | Passed, 26.867s execution. |
| `make test-materials` | Passed, 28.916s execution. |
| `make test-projects` | Passed, 31.584s execution. |
| `make test-language-all` | Passed, 18.109s execution. |
| `make test-tools` | 125 cases / 27,270 assertions, 26.627s execution. |
| Changed C, clang-format 22; `git diff --check` | Passed. |

The callable route has 30 calls at two million instructions, 31 parses and four private JSON calls.
Its header source is below 64 KiB; seven dependencies are below 48 KiB each.
Inputs are below 512 bytes/256 nodes/depth 32. Output caps are ten symbols, two
scopes, three functions, two defaults, one body and four uses/diagnostics; this
corpus publishes no constructors. Membership banks have sixteen cells, with
64 types and 128 IDs. A measured header survives all producer owners.

Reader now has 50 calls at 500,000 instructions, 39 parses and four private JSON
calls. Its added deep case has 497 bytes, 413 nodes and depth 138, within the
reviewed 512-byte/512-node/depth-256 bounds. Original requests and ownership
projections retain their previous bounds. No runtime files, children, services
or GPU work occur in either focused route.
Python 3.12.14 is selected through Homebrew PATH. Logs and static link evidence
are under ignored `.chibi/evidence/toolchain-m5`; this checkpoint uses
`headers-`, `reader-` and their build logs. No scratch executable or project
copy remains.

The earlier checkpoint saved three unintegrated drafts: `unit_analysis`,
`union_members` and `effect_analysis`, each with C helpers, authored source and
pico fixtures. Unit analysis has since passed as recorded above. Union members,
effects and record members are now wired and merged after all five full semantic
selectors passed run 39140946564 on the exact merged tree. The earlier snapshot
syntax failure and its `unordered` local-name correction remain recorded above.
Active production adapters remain excluded. The workload records below retain
the reviewed bounds; none claims execution of the new transport integration.

- Unit analysis: 29 requests, 58 calls at 500,000 instructions and 14 parses.
  The prepared failing baseline retained the old recursive evaluator and incorrect
  zero-capability guard; its 81-expression chain and capability-zero witnesses
  failed before the merged repair. The iterative evaluator preserves cache visits
  and dependency decisions; diagnostics
  retain the existing positional sort and unspecified equal-position ordering.
- Union members: 24 requests, 48 calls and 32 parses. Source calls allow two
  million instructions; raw cycle validation allows sixteen million. The raw
  depth boundary uses 257 definitions/fields, at most 66,049 nominal comparisons,
  258 traversal calls and 514 persistent state writes. Positive native task
  capability-result metadata remains uncovered by this corpus.
- Effects: 34 requests, 68 calls at 500,000 instructions and 10 parses. One raw
  query/cycle graph has 85 expressions. Preserve the existing function-count-plus-one
  propagation bound, exact diagnostic phases and stable binding ordering.
  Actual analyzed-source replays do not establish initial effect propagation;
  separate raw seed/cycle cases cover that behavior.

- Record members: 26 requests, 52 calls at 500,000 supplied instructions,
  32 parses and four runtime JSON decodes. Eight selection requests, thirteen
  member requests, three refreshed-fact boundary requests and two publication
  continuation requests retain the reviewed finite corpus. Defaults are not
  analyzed or executed; layout and final record publication remain native.

This verifies callable headers and annotation reading, not a complete compiler
bootstrap. Prior checkpoints prove unit algebra, type construction, named/lexical
binding and the snapshot component's executable closure in the private runtime
host. Remaining
semantic analysis, lowering, authored artifact construction, scanner ownership
and stage-one/stage-two equivalence still need implementation and validation.
Full-size scalar evaluation
exhaustion was not tested at this checkpoint. No compiler implementation or stage-zero recovery
path is deleted.

PR #420 remains open at `ff6d06c888fe660894776255ec7f9fc507db6393`; PR #407
remains open, conflicted and explicitly held at
`c541133b689c620a377982ee339c017b6d12b263`. Neither branch was imported.
M3's installed Linux/macOS evidence remains in its committed checkpoint. This
checkpoint does not establish a new installed-SDK or Linux claim.
Full native/render/Studio and browser/device suites were not run at this checkpoint.

### M5 snapshot-to-reader composition

`tools/compiler/analysis_state/State.chibi` adapts retained snapshot documents
to the existing signature-reader tree with `TreeForDocument` and exposes
package-local/global document maps in graph-member order, which is sorted by
path. It consumes the snapshot's raw flags and syntax root without reparsing or
resolving names. `language-snapshot-reader-chibi` passes 3 cases / 61,885
assertions in 3.1s and `language-signature-reader-chibi` 4 cases / 128,443
assertions in 6.6s on macOS.
It does not implement header composition, semantic phases, exports, lowering,
artifact output or `Checked`, and makes no M5/self-hosting completion claim.
