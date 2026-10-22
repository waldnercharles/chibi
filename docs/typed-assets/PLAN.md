# Compiled Chibi assets and configuration

## Approved completion criteria — 2026-10-21

- Remove the former declarative format from every active asset, producer,
  consumer, fixture, instruction and dependency in the engine and Studio.
- Compile asset source with the Chibi-authored Compiler.
- Migrate every tracked asset and embedded fixture, preserving its values.
- Delete the former parser, emitter, vendor library and build/package wiring.
- Put source policy, validation and writing in Chibi. Keep native code limited
  to runtime invocation, owned value transport and platform operations, following
  the repository-root self-hosting plan.

This supersedes the earlier scalar-prototype slice. That slice added native
source policy and left the other asset families and dependency in place. Its
local validation does not establish completion of this migration. Generic Bake
remains deferred. Parameter editing, computed helpers, explicit overrides, Reset,
source history and save must continue through the shared source path.

## Ownership and baseline

Engine: `codex/typed-assets-20261021`, updated to `881941162` before this rework.
Studio: `codex/typed-prototype-editor-20261021`, based on `97f74c7e`.
Both worktrees are under `.worktrees/typed-assets`. Dirty primary checkouts and
unrelated worktrees remain separate. No commit, push, merge or deployment has
occurred for this work.

The initial inventory counted 238 engine and 150 Studio asset files, plus
embedded native fixtures and transport strings. It covers project manifests,
scenes, prototypes, graphs, input maps, scenarios, component presentation,
bootstrap metadata and documentation configuration. Retired standalone game
repositories remain read-only under the workspace instructions.

## Source and runtime contracts

Authored assets are ordinary Chibi packages. A public `Build() -> byte[]` entry
uses typed constructors from `game:assets`; ordinary functions, branches, loops
and imports go through the authored Compiler. Family constructors enforce stable
schema fields. Reflected component data keeps the existing catalog validation.

The shared value library and source writer are Chibi. Their JSON bytes are a
private value transport into the existing native document codec, with exact
integer widths, ordered objects, arrays, nulls and finite real values. Source is
never a JSON string passed to a renamed parser. Clipboard, checkpoints and
machine protocols use the JSON codec directly.

The native boundary loads prepared code, provides an immutable library snapshot,
invokes the authored compiler and pure asset entry, and owns the returned values.
No file, process, clock or random capability is granted to asset code. Shared
execution limits and cancellation cover nested calls. Source and evaluated values
remain distinct. Source edits use CST ranges and expected bytes, preserve helper
expressions and comments, and reject unsafe rewrites.

Bootstrap version policy is compiled into the compiler package. Early recovery
reads the existing artifact metadata, then checks the recovered compiler's
current contract. This avoids a new native source parser or a compiler-selection
cycle. Documentation schemas and validation are authored Chibi; filesystem
confinement and atomic publication remain platform operations.

## Work and deletion gates

1. Build the shared typed value library, compiler facade and authored writer.
   Prove actual recursive values, helper evaluation and compiler diagnostics.
2. Supply the portable runtime bridge and target-compatible immutable compiler
   artifact/library bundle in source, installed SDK and browser builds.
3. Migrate engine and Studio assets, embedded fixtures, discovery, creation,
   editing, saving, reference paths and protocols together. Projects become
   version 33. Retire historical format-specific migration implementations.
4. Move source mapping, reconciliation and parameter/override policy to Chibi.
   Keep the source session's validation and single undo transaction boundary.
5. Remove the old parser/emitter, vendor directory, exported components, sources,
   headers, defines, include paths, licenses and packaging references together.
6. Check source and relocated SDK builds, Studio bootstrap and browser packaging;
   inspect final tracked files and linked/installed inventories for leftovers.
   Use the existing production supervisor for any authorized runtime checks.

A one-time ignored migration utility may read the old format while converting
tracked data. Delete that utility and its outputs before completion. It is not a
compatibility path or an installed dependency.

## Status

Implementation is complete in the two worktrees. All 238 engine and 150 Studio
assets from the initial inventory, embedded fixtures, producers and consumers
use compiled Chibi modules. The retired parser/emitter, vendor library, scalar
evaluator, frozen migration implementations and dependency/export/license wiring
are deleted. One-time migration utilities and snapshots have been removed.
Tracked and untracked source/path audits found no retired-format references;
active build outputs and both installed SDK inventories are clean.

Source writing, syntax metadata, parameter changes, reconciliation, materializing
an explicit detach, value encoding, schemas and resource generation are Chibi.
Native code supplies runtime invocation, owned transport, reflection and platform
operations. The obsolete prototype-specific grammar changes were removed.

### Validation performed

These are production builds and compiler invocations, not runtime suite passes.
All supervised runtime commands used the unchanged 60-second ceiling.

| Work | Result | Evidence under `.chibi/` |
| --- | --- | --- |
| Engine/controller/compiler and existing fixture drivers | Native build passed | `asset-rework-final-native-build.log` |
| Native SDK and Studio | Packaged and rebuilt | `asset-native-sdk-package.log`, `asset-native-studio-build.log` |
| Browser SDK and Studio | Built, packaged and installed | `asset-rework-web-build.log` |
| Installed and relocated public SDK consumers | Configured and compiled | `asset-sdk-consumer-installed.log`, `asset-sdk-consumer-relocated.log` |
| Studio starter project | Normal compiler check passed, 9.376s | `asset-starter-project-compile.log` |
| Recursive values, helpers, exact integers, Unicode and finite reals | Native compilation and evaluation passed | `asset-schema-proof.log` |
| Whole-record prototype overrides | Compiled and evaluated, siblings preserved | `asset-overrides-proof.log` |
| Negative unsigned integer | Compiler rejection with mapped source range | `asset-negative-proof.log` |
| Wasm Compiler followed by asset Build | Native output parity, 1.844s | `asset-wasm-proof.log` |
| Largest migrated asset, 96,181 source bytes | Compiled and evaluated, 15.634s; 1,864,944 prepared bytes | `asset-largest-build.log` |
| Installed documentation command | Eight pages generated, 50.511s | `asset-installed-docs-build.log` |

Both native64 and wasm32 resource artifacts match their manifest checksums and
all 34 source fingerprints. Native SDK identity:
`77d65d89843c583cfa6a2b1a2b526192c4818525ca4c4af5e0e8b3c4ecf88ac9`.
Browser SDK identity:
`2566ee31796e298b53457797afe274f6a77c346191cbbc6e76640697bd47de6b`.
Studio's 476 extraction consumer hashes and 269 current provenance records were
verified against the migrated files and immutable origin records.

### Limits and delivery state

Full native, render and Studio suites were not run. The former scalar evaluator's
three standalone prototype admissions were withdrawn; the rewritten compiler
workloads require fresh review before execution. Existing fixtures were compiled.
Earlier scalar-slice results do not establish replacement-path runtime behavior.

Cold compilation in the native Debug configuration takes about five seconds for
a small distinct asset. The thread cache holds up to 128 prepared assets/128 MiB;
there is no persistent asset cache or measured full Studio startup result. Source,
transport, value, depth, instruction and cancellation limits are implemented;
a recoverable aggregate VM allocation quota remains separate work. Arbitrary
computed source shapes can be evaluated, but edits that cannot preserve their
source structure return a diagnostic.

Validation used macOS arm64 and Emscripten/Node. Linux and Windows were not built
in this rework. Changes remain uncommitted and unpushed in the two named
worktrees; they are not merged or deployed. Dirty primary checkouts are unchanged.


## Approved stabilization follow-up

The owner approved starting on 2026-10-22: checkpoint this migration, integrate
current main and its existing compiler performance work, add persistent compiled
asset caching, and establish focused runtime/editor validation. Current main now
contains result-location construction from PR 642. Shared-string and ownership
changes remain with their existing worktrees and semantic discussion.

### Plan and system impact

1. Review and checkpoint both owning worktrees locally, recording unexecuted
   runtime coverage. Integrate current engine main and reconcile new graph/test,
   SDK and license changes without restoring the retired format or dependency.
2. Keep persistent cache decisions in authored Chibi. Native code provides
   bounded owned transport and explicit cache storage outside pure asset code.
   Cache prepared code only, so every load still evaluates Build with the
   current cancellation and instruction limits. The key covers source/path,
   explicit imported sources/collections, compiler bytes and target/catalog ABI.
   Treat missing, stale, malformed, truncated and incompatible entries as misses;
   publish complete entries atomically. Cache storage is optional and cannot make
   valid source fail when unavailable. Respect concurrent workers and processes.
3. Rebuild compiler/bootstrap/resource closures after compiler ABI changes and
   build the SDK, native/browser Studio and relocated public consumers. Review
   actual compiler work and cache state before restoring standalone runtime
   admissions. Check cold/warm behavior, key invalidation, corrupt entries,
   parameter/override/Reset/history/save, and actual evaluated migrated values
   under the unchanged shared 60-second runtime supervisor.
4. Record remaining work accurately. Aggregate recoverable allocation ceilings,
   automatic project-helper snapshots and broader schema/edit patterns are next
   increments; do not fold in paused ownership semantics or legacy compatibility.

Critical files: `src/authoring/asset_compiler_host.[ch]`, authored compiler/source
services and resource generator, `tools/graph/engine/{Engine,Targets}.chibi`,
`sdk/asset_documents.cmake`, SDK packaging/export code, existing engine and Studio
fixture dispatchers and test policy, plus owning project/session lifecycle calls
that select optional cache storage. Extend shared APIs rather than adding a
separate compiler or test executable. No publishing or merging into main is
implied by this local stabilization checkpoint.

Status: planning/review before the migration checkpoint. Cache design and renewed
runtime workload review are proceeding independently while sources are frozen.


Pre-checkpoint review found follow-up correctness work, so the local commit is a
WIP recovery point rather than publication readiness: validate decoded Build
output before recursive native conversion; align projected value depth with the
1024-level authored quota; reject unsupported decoded embedded NUL until the
string representation supports it; verify complete source inventory and hashes
before packaging compiler resources; and regenerate stale resources from a clean
build without assuming its tools already exist. These are stabilization tasks.
