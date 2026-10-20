# Test iteration

## Fast, targeted tests

Use deterministic C pico_unit tests as the primary regression checks. Cover a
specific behavior and its failure cases through the smallest public runtime
interface that exercises it. Advance a synthetic clock for time-dependent
behavior; assert values, ownership, geometry, callback counts and work counts
rather than elapsed-time thresholds. Keep compilation separate from execution.

The goal is for every test to be fast and targeted. When replacing a smoke test,
map each meaningful assertion to its replacement first. Remove duplicate
entrypoints and measurement-only loops from regression commands. Keep unique
pixel, renderer, backend and authored-workflow assertions until a focused test
covers their contract. A short unit result does not satisfy unrelated integration
requirements or clear a PR hold. The broad suites below still need migration.

## Run the checks

The engine build compiles the full test drivers, `test_tools` and
`test_language`, with every native, compiler and renderer package. CI runs each
group below on every push to `main` and every pull request into it that changes
more than documentation, and `make build` links `chibi`, `chibi-compile` and
`chibi-player` in the same tree.

```sh
make test-build          # chibi_ctl and both drivers in build/ (ASan and UBSan by default)
make test-native         # admitted native group, one 60-second deadline
make test-materials      # the material simulation selectors, one 60-second deadline
make test-projects       # project, asset import and chibi_ctl selectors, one 60-second deadline
make test-language-all   # admitted native language aggregate, one 60-second deadline
make test-bootstrap      # stage one compiles itself, stage two matches it (Release build)
make bootstrap-artifact  # rewrite the checked-in recovery artifact from stage two (Release build)
make bootstrap-recover   # recover the compiler from that artifact and the sources on disk
make test-recovery       # recover the compiler from that artifact and HEAD's sources
make test-tools          # tooling and process integrations, separate build-tools/
make agent-test AGENT_SELECTOR=--source-edit
```

`make test-projects` also builds an unsanitized Release `chibi_ctl` in
`build-release/` for `validate-project`, `prototype-expansion`, `controller` and
`render-fixture`. `validate-project` starts `chibi_ctl` dozens of times; each
start builds the authoring catalog, and under ASan those starts alone take
longer than the deadline. The project selectors left the native group when it
reached 58 of its 60 seconds on a 4-core Linux container (2026-10-11): feature
coverage, mesh files, project validation and model import alone took 33 s.

`python3 tools/test_policy.py tool <name>` runs one entry of that table against
the drivers `make test-build` produced and that Release `chibi_ctl`, except
`language-package-compiler-chibi-bootstrap`, which runs the Release
`test_language` that `make test-bootstrap` builds; set
`CHIBI_TOOLS_BUILD_DIR`, `CHIBI_LANGUAGE_BUILD_DIR` or `CHIBI_CTL` to use another
build.

`python3 tools/test_policy.py tool fixture-format` checks the fixed thirteen
checked-in fixture projects with Release `chibi_ctl --format --check`, under
one shared 60-second deadline. It refuses outdated manifests before launching
any child, because opening an old project can migrate it. This standalone
admission does not enable the quarantined full gate. Other standalone admissions,
including `language-query-program` and `language-row-array`, are also outside the
aggregate groups; use `make test-list` for the current inventory.

`make language-build` builds a standalone, faster `test_language` in
`build-language/` for the `test-language*` targets. Add a case to the selector
that owns its area; add a selector to `GROUPS` in `tools/test_policy.py` so the
group, and therefore CI, runs it. Studio's checks live in `chibi-studio`.
`radiance`, `radiance-depth`, `radiance-surface`, `radiance-horizons4`,
`radiance-horizons8`, `material-pixels`, `material-sampling`, `scene2d`,
`scene3d`, `scene3d-gles`, `scene3d-gi`, `scene3d-gi-gles`, `material-ramps`,
`shader-parity`, `shader-programs`, `material-cameras`, `mesh-materials`,
`sdf-quality`, `scene3d-post` and `scene3d-shapes` render through a hidden
window, which needs a display and a GPU driver the CI job does not set up, so
they stay out of the group; run them with
`python3 tools/test_policy.py tool <name>`.

CI runs the build, the four groups and `make test-tools` on Linux and macOS. On
Windows it builds the SDK tools, compiles `test/fixtures/studio` under a
one-minute step timeout (the test supervisor needs POSIX signals) and builds
Studio against the installed SDK, without running tests. The Windows jobs are
advisory until they first pass.

## Feature coverage

`test/fixtures/feature-coverage` is the all-features project: one scene per
area (2D and 3D physics, tilemaps, UI, particles, audio, save, rhythm,
materials, input and entities). `test_tools --feature-coverage <project> <chibi_ctl>` compares the engine
catalog (components, native functions and events) with what the project's
scripts, prototypes and scenes use, prints each gap, and runs every scene for
four frames through `chibi_ctl`'s `run_scenario`, failing on any behavior
failure. The production policy passes the selected `CHIBI_CTL` controller,
defaulting to `build-release/bin/chibi_ctl`. Every prototype, behavior, library and system must be reachable from a
scene: placed or attached there, spawned or called from source that runs, and
only that source counts. A spawn reaches the prototype it names directly or
through a value, such as a local, state or library parameter that holds the
prototype's name, as find references does. A scene entity with its own behavior list
runs that list in place of its prototype's; otherwise it and a spawned
prototype run the prototype's list. A prototype's children spawn with each of
its instances, so they are reached and run as instances too. Components count on these placed and
spawned instances, after the scene's overrides, and where running source reads,
writes or adds them, directly or through a generic call such as
`add_component(Heat)`; a system query alone matches nothing without
them, so it does not count.
A system runs only while an entity's `EngineSystem` names it. Components the
project declares in `content/components` load as they do in preparation and
count as its own catalog entries. Run it on any project whose content folder is
`content` to list what that project leaves unused; it fails while any of that
is unlisted.

The project's `coverage-gaps.txt` lists the entries no scene uses yet, in groups
under a comment that gives the reason; without the file, every unused entry is a
gap. The check fails when a catalog entry is in neither the scenes nor that
list, when a listed entry has no reason comment above it, and when a listed
entry becomes used. A PR that adds a component, native function or event uses it
in a feature-coverage scene; it lists the entry as a gap only with a reason. The
`projects` group, and so CI, runs it on the fixture.

## Temporary 60-second policy

The compiled-runtime rollout passed native, language, tooling, materials and
project groups under their shared 60-second deadlines. Browser Wasm execution
passed in serial and pthread configurations. URL gesture coverage passed nine
cases; the browser audio transport passed two C cases (six assertions) in
1.372 seconds including its native launcher. Permission and capture endpoints
are mocked; this does not claim real external recording coverage.

Negative controls reproduced the helper-name migration, particle-as-material,
resize pointer and delayed-audio bugs before the fixes. The particle preview
rendered 128 particles and an occluding box in two GPU frames at 320×180 on
Metal/Cocoa (3.630 seconds including startup). This is rendering evidence, not
a steady-state performance measurement. The broader `scene3d` check aborts in
the existing sky path because Metal rejects an unused 128-byte instance-buffer
layout; the sky source, Cute revision and instance-buffer patch are unchanged.
Full native/render/Studio coverage remains disabled, not passed.

`player-url` runs nine C-owned URL gesture lifecycle cases through nine short
Node invocations. It drives the production browser bridge with fake browser
events and records URL-open requests without a browser, network or GPU. Its
first combined supervised run passed nine cases and 67 assertions in 2.278
seconds.

`player-clock` compiles the player's actual clock initialization for both native
and browser hosts and records the Cute limiter setter. Two C cases verify that
native enables 60 FPS pacing while the browser leaves pacing to
requestAnimationFrame. Both cases and four assertions passed within the native
group's 3.534-second run; restoring the unconditional setter fails the browser
case. The cases use no clocks, sleeps, windows or GPU.

The browser player previously enabled Cute's native 60 FPS sleep/spin limiter
inside requestAnimationFrame. Removing that second limiter changed bounded
Stillwater samples from 58.8 FPS to 120.0 FPS in both Solo and Duet on the same
browser at a 2560×1440 canvas (1280×720 CSS, DPR 2). Each sample discarded 120
warmup frames and measured 2.5 seconds. Mean synchronous frame-callback time
fell from 16.7 ms to 3.08 ms for Solo and 3.46 ms for Duet; the respective 95th
percentiles were 4.5 and 5.0 ms. Callback time excludes asynchronous GPU
completion. These are short local measurements, not a sustained or cross-device
frame-rate guarantee.

`audio-analysis` runs eleven fixed PCM, frequency-band, amplitude, silence,
source-selection and snapshot cases, plus one dummy-SDL mixer lifetime case.
The mixer case shares a two-second callback deadline. It opens no graphics or
real recording device. `audio-browser` separately drives the Wasm capture bridge
through the C browser harness; it requires the matching browser test build and
is excluded from the native audio aggregate.

`shader-webgpu` compiles three generated programs with Cute's runtime shader
compiler: the noise fragment program and particle vertex programs with and
without Index usage. The vertex cases read the checked-in shader source, limited
to 64 KiB, and inspect WGSL and Metal output. They open no app or GPU device.
`shader-parameters-prepared` runs nine bounded CPU cases covering parameter
codecs, model and particle staging, signatures and helper linking, rejected mesh
material use, preview reload/rename failures, and source-budget reclamation for
129 zero-count particle entities. GPU particle rendering remains separate.
The combined candidate's first supervised runs passed all twelve audio cases and
298 assertions in 1.238 seconds, and all three shader compiler cases and 32
assertions in 0.770 seconds. These measurements do not cover browser capture,
GPU particle drawing, or the expanded parameter selector.

The retired frontend experiment's five source inputs are preserved as historical
syntax fixtures in `test/fixtures/language/prototype`. Its earlier parser tests
do not establish production semantic or execution coverage.

The production syntax dispatcher builds independently with `make language-build`.
`make test-language` runs seven finite C syntax/recovery/incremental snapshot cases
and the explicit `.chibi` corpus manifest under one production deadline. Corpus
input is limited to 256 files, 512 KiB per file and 8 MiB total. Independent review
approved first supervised measurement; a syntax pass does not establish semantic
or execution correctness. The dispatcher compiles against public SDK headers;
its syntax cases use allocation support and Tree-sitter's C runtime.
Default inputs are curated engine examples and fixtures. Extracted projects can
be included through semicolon-separated absolute roots in
`CHIBI_LANGUAGE_CORPUS_PROJECTS`, or an absolute
`CHIBI_LANGUAGE_CORPUS_MANIFEST` containing one existing absolute `.chibi` source
path per line. Save the generated `corpus.txt`
with evidence so the exact checked file set remains identifiable.

`python3 tools/test_policy.py tool language-semantic` selects seven independently
reviewed C cases with tiny inline sources and a fixed catalog. They check lexical
resolution, optional component guards, numeric limits, catalog operator dispatch,
transitive effects, binding cycles and invalid-source recovery. There are no files,
child processes, engine frames or GPU work. Debug Clang 23.1.1 with ASan/UBSan
passed 7 cases and 36 assertions in 0.468 seconds on 2026-10-07.

`python3 tools/test_policy.py tool language-execution` selects ten independently
reviewed C cases through public SDK headers. They check VM/native results and
state changes, host calls, owned values, linked functions, malformed instruction
rejection, a loop with a three-step budget, integer bitwise/shift behavior at
all eight signed and unsigned widths, and optional context-aware logging. Native code generation is bounded
to these small functions; there are no files, processes, frames or GPU work.
Debug Clang 23.1.1 with ASan/UBSan passed 10 cases and 726 assertions in 0.577
seconds on 2026-10-07, including actual VM and SLJIT execution.

`python3 tools/test_policy.py tool language-program` selects twelve independently
reviewed source/runtime cases on VM and SLJIT. Besides typed events, systems,
bindings, imports, cancellation and failure records, it exercises eager tuple
copies and declaration-scope lazy tuple initialization. Four tiny lazy consumers
check one producer call, sibling initialization on a field write, complete
replacement suppressing the producer, and continuation capture. The managed
payload case uses root/outlined variants, 256 supplied-time pumps and strings up
to 2,624 bytes, checking retained values and bounded live/cached storage. Invocation
budgets remain 1,000 steps. There are no engine frames, processes, sleeps or GPU
work. Debug Clang 23 with ASan/UBSan passed 12 cases and 1,638 assertions in 0.881
seconds on 2026-10-07 (`.chibi/architecture/language-program-fields.log`).

`python3 tools/test_policy.py tool script-benchmark` measures default compiler
execution of equivalent inline arithmetic and authored scalar helper calls on
VM and the platform's compiled backend (SLJIT natively, Wasm on the web).
Four in-memory modules each run one correctness warmup and five
trials of 200 dispatches with 100 loop iterations: 400,400 total iterations.
pico_unit checks the exact result before timing and after each trial. Output
reports monotonic elapsed time, allocation-wrapper calls/requested bytes and
arena block allocations; requested bytes are cumulative, not live memory.
Compilation is excluded from trial timing. This CPU-only selector has no engine
frames, GPU, files, processes or sleeps and runs under the shared 60-second
production supervisor deadline. The permanent selector passed 64 assertions in
0.215 seconds on 2026-10-11 (macOS arm64, Clang 23, Release, no sanitizers).
Set `CHIBI_LANGUAGE_BUILD_DIR` to the Release build's `test/language` directory
when comparing performance; the default build may enable sanitizers.

Against `f3ff608c1`, completed-call storage reuse reduced median helper-loop
dispatch time from 24.220 to 11.250 microseconds on SLJIT and 59.985 to 41.450
microseconds on VM. Each dispatch makes 100 helper calls. SLJIT allocation-wrapper
calls fell from 710 to 17 per dispatch and requested bytes from 643,888 to 9,512;
VM calls fell from 912 to 21 and bytes from 691,104 to 10,792. Each activation
retains at most one completed frame with at most 64 KiB of backing storage, then
frees it at teardown. These are CPU microbenchmarks, not rendered frame rates.
The Tetris `8cac6a7` project uses an input contract newer than this baseline, so
no whole-game speedup is claimed from these measurements.

Browser JIT checks use `make test-wasm` and `make test-wasm-browser`. Add
`WEB_THREADS=1` to build and test the worker configuration. Both compile the
same C language dispatcher with Emscripten Release, embed its small language
examples, and run under the production 60-second deadline. Configuration and
build time are separate. Node runs the Wasm, execution, program, headless,
publication and scalar benchmark selectors. Chrome runs the same selectors
except headless through `test_tools --wasm-browser`, with GPU use disabled and
a 55-second internal process deadline. The fixture owns and removes its browser
profile. `CHIBI_WASM_BENCHMARK_ONLY=1` selects only the fixed C benchmark for
baseline comparison.

Chrome also runs `--row-array`, `--modules` and `--query-program` for packed
numeric arrays, imported generic ownership, maps and iteration. These are fixed
C workloads; module paths use a temporary MEMFS directory and source overlays.
The packed loop has 64 elements and 16 trials per numeric width, representation
and backend. The combined route retains its 55-second internal deadline and
60-second supervisor limit. Its expanded duration needs a supervised measurement.

Wasm cases assert actual generated-entry calls, exact integer widths, arrays,
helper continuations, budgets, prepared generation ownership and canonical
scratch capacity after a smaller VM body. The cache case keeps 257 functions
live and checks that repeated passes add no compilations. Separate checks cover
retirement without recompiling unrelated functions and pthread table cleanup.
The default-backend and scratch-capacity checks failed with their prior behavior
restored. Linking the previous loader made both cache regressions fail on their
compilation-count assertions. Full GPU/Studio coverage remains quarantined.

On 2026-10-11, Chrome passed 91 cases/5,007 assertions in 0.907 seconds; its
pthread configuration passed 92 cases/5,041 assertions in 0.943 seconds. Node
also passed both configurations, including the headless selector. The native
language group passed under ASan/UBSan in 57.116 seconds. These are CPU checks;
they do not validate browser rendering or establish a 60 FPS Tetris result.

The Release benchmark compared the original VM at `06e7b1a2b` with this Wasm
backend using the same C workload, Emscripten settings and exact output checks.
Five trials produced these median microseconds per dispatch in Chrome:

| Workload | Original VM | Current diagnostic VM | Wasm JIT | Speedup over original VM |
| --- | ---: | ---: | ---: | ---: |
| Inline arithmetic | 25.900 | 28.475 | 11.850 | 2.19x |
| Authored helper calls | 48.525 | 56.925 | 36.700 | 1.32x |

Extracting shared runtime effects makes the diagnostic VM 9.9% and 17.3% slower
in these workloads; the speedups above use the original VM as their baseline.
Node measured Wasm speedups of 2.23x and 1.40x over that same original VM. The
pthread Chrome build measured 11.450 and 35.475 microseconds per Wasm dispatch;
the original baseline was serial, so this is not a matching pthread comparison.

Chrome Wasm publication took 1.065 ms for the inline module and 0.190 ms for the
helper module. These are individual cold observations for small modules, not
compile-time percentiles or bounds for a whole game. Publication is synchronous;
workers also pay compilation on first use. Steady execution reuses live entries.
Samples, build provenance, artifact hashes and logs remain under
`.chibi/wasm-jit/benchmark-final.json` and `.chibi/wasm-jit/baseline/`. Temporary
baseline and sanitizer builds have been removed.

`python3 tools/test_policy.py tool language-navigation` selects eleven reviewed
in-memory cases for shadowing, capture rejection, cross-file renames, escaped
names, event aliases, reflected members, signatures, completion recovery and
owned output lifetimes. It passed 99 assertions in 0.678 seconds with ASan/UBSan.
`language-publication` selects four reviewed VM/SLJIT cases for atomic rejection,
name swaps/removal, pending-work preservation and shared root/import state after
all owners detach, and opt-in source profiling. The profiling case checks actual
entries and resumes, origin retention after removal, disable and reset. All four
cases passed 226 assertions in 0.964 seconds with ASan/UBSan after fixing selection
of the builder's unnamed source sentinel. The later grammar
and authoring integrations require their own affected validation. The expanded
13-case navigation selector now passes 130 assertions in 0.864 seconds with
ASan/UBSan, including exact KDL declaration/reference edits, local event alias
resolution, and hover/highlights over external documents.
The 14-case extension passes 144 assertions in 0.743 seconds with ASan/UBSan.
It adds exact RML event-token renaming while preserving surrounding markup,
comments and unrelated names, plus stale-span and unsafe-token rejection.
The 16-case capture passes 195 assertions in 0.614 seconds with ASan/UBSan.
It covers recovered assignment and prototype contexts, exact library completion
ranges and local shadowing. The 13-case execution capture passes 792 assertions
in 0.479 seconds on VM/SLJIT, including copied synchronous outcome identities,
nonzero outcome selection, result payloads and malformed outcome rejection.
The 13-case program selector now passes 1,689 assertions in 0.884 seconds with
ASan/UBSan on VM and SLJIT. Its new fixed case checks exact signed/unsigned 64-bit
limits, values above 2^53, contextual nominal enums and five out-of-range rejects.

`python3 tools/test_policy.py tool controller` runs five C command-response cases
against `build-release/bin/chibi_ctl`, also included in `make test-projects`.
Set `CHIBI_CTL` and `CHIBI_TOOLS_BUILD_DIR` to select isolated builds. Independent
review admitted seven tiny generated files per case, two scene entities, thirteen
owned child processes capped at ten seconds each, and twenty total fixed
simulation steps under the shared sixty-second deadline. Each fixture is formatted
through `chibi_ctl --format`, and its files are removed after the case.

The cases check canonical definitions and references, exact rename edits without
file mutation, origin/location round-trips, invalid-draft diagnostics, graph and
system inspection, pin/layout flags, and prepared execution backends. Runtime
profiles match sample generation IDs to registered modules and check per-handler
entries, a delayed resume, owner identities, source ranges, and measured frame
accounting. They cover profiling disabled, zero warmup, and zero measured frames
after warmup. Sampling starts only at the first measured frame. Module IDs are
compared within one response; they are not cross-process identity guarantees.
These CPU command checks do not exercise Studio, rendered graph gestures, GPU
profiling, or performance thresholds.

`python3 tools/test_policy.py tool runtime-scene` runs two public SDK consumer
cases with four entities/two scene instances, owned component payloads, deferred
Begin, rollback and cancellation. The normal Debug runtime-only consumer passed
59 assertions in 0.656 seconds. `field-runtime` originally ran three public cases for owned
scalar artifacts, a 3-by-3 whole/sliced bake, geometry/parameter results, invalid
IR and every prefix of a prepared record smaller than 512 bytes. It passed 254
assertions in 0.828 seconds with ASan/UBSan. Two of its cases check every field
node's library host call against the field evaluator, sampled and baked, over
2,000 operand sets each, and that a call's Seed Offset is its whole seed.
Neither selector loads a renderer or runs engine frames.
`prototype-document` selects one independently reviewed case with two small
reflected types and fixed creation-default, detached-component, value snapshot,
partial KDL and malformed-size operations. It has no files, worlds, processes or
GPU work. The standalone consumer uses the public `Chibi::prototype_document`
interface; it passes 21 assertions in 0.928 seconds under ASan/UBSan.
`ui-authoring` selects two independently reviewed public SDK cases with fixed
RML/RCSS snapshots, formatting and attribute-edit reparses, owned text after
producer retirement, last-valid state after malformed RCSS, and exact event token
ranges. It has no files, processes, worlds, or GPU work.
Its independently reviewed fourth case adds typed catalog overrides, revision
replacement invalidating an existing session, new default values and a rejected
wrong parameter type on a fixed 3-by-3 bake. All four cases pass 267 assertions
in 0.650 seconds with ASan/UBSan after the typed catalog extraction.

`--source-presentation` is an independently admitted native selector for seven small
CPU cases: dedicated clone ownership after compiler retirement, explicit source
edits, exact-source layout metadata, invalid drafts, native palette declarations,
callable-local ownership and readable formulas. Formula coverage retains authored
call origins and effectful operations and reparses expression replacements.
These cases pass 7,971 assertions in 1.259 seconds under the production deadline.
Later document/Studio integrations are unverified.
It also runs two component presentation cases on two fixed in-memory documents:
the engine's tabs, icons and reflected units and ranges, and a document
overriding them key by key.

`make agent-gpu-test AGENT_SELECTOR=--studio-scene-camera` admits one fixed
native Studio camera case, with one in-memory cube and thirty hidden 1920x1080
frames at fixed 1/60 dt under one shared 60-second deadline. Queued SDL input
exercises orbit, pan, anchored dolly, same-frame right-button/W flight,
fractional diagonal wheel pan, an above-horizon press, focus-loss cleanup and
checkpoint lens retention. This does not admit the broader Studio acceptance
suite or establish web input coverage.

Use the existing absolute `CHIBI_STUDIO_SOURCE`, `CHIBI_STUDIO_BUILD` and
`CHIBI_STUDIO_ACCEPTANCE_EVIDENCE` paths. The evidence must match the current
acceptance binary, SDK, compile inputs, extraction metadata, every file under
`tests/acceptance`, and the recorded Studio/UI resource generation. Refresh that
complete source manifest after camera fixture changes; do not reuse an earlier
sixteen-file snapshot or omit new files. The existing 8–32-file, 256 KiB-per-file
bounds and file/directory symlink rejection still apply. Restore the source
`projects/studio/project.kdl` into the build's `share/chibi/studio/project.kdl`
before each independent run, as Studio may migrate that build copy on startup.
Exactly one `Studio 3D camera accepted: frames=30` line is required; incomplete,
repeated or diagnostic-bearing output fails. C policy regressions check those
completion requirements. The first supervised camera execution remains pending.

`make agent-gpu-test AGENT_SELECTOR=--studio-scene-gizmo3d` admits the
independently reviewed native gizmo fixture: six in-memory prototypes and eight
actors, forty-two hidden 1920x1080 frames at fixed 1/60 dt under one shared
60-second deadline. Queued SDL gestures exercise world/local movement, grid
snapping, Orientation and mirrored-parent rotation, local scaling, undo/redo,
Escape, focus-loss and hidden-panel cancellation, and refusal beneath sheared
or singular parents. It uses the same installed-SDK provenance, disposable
three-file project and resource-generation checks as the camera route above.
Refresh the complete acceptance source manifest after fixture edits (twenty
files with the shared 3D fixture and collider acceptance).
Exactly one `Studio 3D gizmo accepted: frames=42` line is required; an older
38-frame fixture, incomplete run, duplicate marker or runtime diagnostic fails.
The C policy case checks completion and rejection; the first supervised GPU
execution remains pending. This admission covers only this fixed fixture.

`make agent-gpu-test AGENT_SELECTOR=--studio-scene-collider3d` admits the
independently reviewed native collider fixture: three in-memory prototypes and
three actors, twenty-six hidden 1920x1080 frames at fixed 1/60 dt under one shared
60-second deadline. Queued SDL input edits a stretched box face, a sphere's
radius and a capsule's height with typed undo/redo, rejects parallel-axis and
nonfinite begins, refuses hidden handles, and cancels a face move with Escape or
H-during-drag while restoring dirty/history state.
It uses the same installed-SDK, complete twenty-file source manifest, disposable
project and resource-generation checks as the gizmo route. Exactly one
`Studio 3D collider handles accepted: frames=26` line is required; incomplete,
repeated, unrelated or diagnostic-bearing output fails. A C policy case checks
those requirements. The first supervised execution remains pending, and this
route admits only the fixed collider fixture.

The four graph selectors compose the application's graph palette (core, shader,
field and native nodes over the game's components) into the registry and edit
in-memory graphs; no files, worlds, processes or GPU work run.
`--graph-document` runs fourteen cases: node comments and comment frames through
graph documents, input and EXEC edge lookups across edits, pastes, remapped IDs,
malformed fan-out and four concurrent first readers, stable node and pin IDs,
reflected Make/Break descriptors, literal defaults and edge replacement. It passes
256 assertions in 0.36 seconds with ASan/UBSan.
`--graph-presentation` runs fourteen cases: which nodes fold inline, arrangement
skipping inline nodes, presentations through asset value documents, expression
text for behavior, component, shader and field nodes, Find in graph and node
hover sentences. It passes 128 assertions in 0.51 seconds with ASan/UBSan.
`--graph-functions` runs eighteen cases: function and macro signature edits,
purity, defaults, moves and variable promotion reaching every call, Collapse to
Macro and Expand Node, and catalog problems of two linked shader libraries. It
passes 170 assertions in 0.50 seconds with ASan/UBSan.
`--vector-nodes` runs one case on the vector kernels: normalize at extremes,
zero scale and the palette's Direction To through a saved graph. It passes 42
assertions in 0.20 seconds with ASan/UBSan.

`--system-schedule` admits only `test_system_schedule`: fixed access/order cases,
62 small CPU engine frames and a serial/parallel 128-entity, 20-frame comparison.
The scheduler benchmark and broad material aggregates remain quarantined. This
selector currently fails: the migrated invalid-source case exposed a NULL program
identity in lowering. The supervised debugger located the failure; its completion
is diagnostic evidence, not a passing test.

`--new-asset` admits one case: `new_asset_create` writes a behavior and a scene
into one temporary content folder, creating the missing folders, and refuses an
existing file, a model named like a shape, an invalid name, a folder blocked
by a file, and a family folder or content folder that is a symbolic link out
of the project. Each refusal's errno tells a bad request from a taken name and
a storage failure. CPU only; the folder is removed afterwards.

`--asset-refactor` admits one case: renaming a prototype that prototype
children use plans a rewrite of each child in its parent's text, and a parent
that includes another prototype keeps its includes and only its own children.
Four in-memory prototypes, no files, CPU only.

Material tests are split by masks/damage, cave/terrain, fluids, body destruction,
fragments and debris; a shared fixture owns setup and coverage invariants.
The fixed largest inputs are a 2048 by 768 cave, a 192 by 192 flow grid,
and two 192-particle MPM pools of 3000 steps each. Worker cases use four workers.
No material selector creates a window, child process or network connection;
the group runs once under one shared 60-second deadline.

The material simulation selectors run without a window or GPU; wall times are
under ASan/UBSan through the production supervisor on a shared four-core host.
`--material-destruction` checks material masks and their cover rectangles,
carving and fragment extraction, contours, callback queues and capacity
fallback, damage rounding, debris, fuel blasts reaching bodies as Material
Impact events, and `CaveField.kdl` baked against an independent reference cave
and streamed as terrain through preview worlds: 26 cases, 2,700 assertions,
2.7 seconds. `--material-fluids` checks the particle fluid's sleeping,
cohesion, liquid splits and merges, body response, gas flow, buoyancy and
mixing barriers, and serial against worker parity of particles and the flow
grid: 12 cases, 34,828 assertions, 13 seconds. `--material-mpm` checks the MPM
solver's transfers, elastic recovery, transactional failure, worker parity,
collider momentum and two 3000-step hydrostatic pools: 9 cases, 13,464
assertions, 4.0 seconds. `--material-thermal` checks heat exchange between
parcels, across walls and openings, dormancy, quenching, worker parity and
authored thermal components: 12 cases, 9,469 assertions, 7.6 seconds.
`--material-simulation` checks contained liquids, sleeping deformation,
expiry, mixed phases, the obstacle and particle indexes, and contact
responses: 27 cases, 179,416 assertions, 0.5 seconds. `--material-ecs` checks
blast craters, constitutive models, appearance detail and fluid impulses that
turn bodies through authored components: 10 cases, 4,737 assertions, 0.9
seconds. `--material-render` checks the material raster's linear coefficients
and the mask LOD levels: 6 cases, 2,723 assertions, 0.2 seconds.

`--material-benchmark` times four fixed fluid workloads on four workers: a DDR
patch and an MPM collision, 32 by 32 parcels for 30 frames, each equal to its
serial run; heat exchange over 48 by 48 parcels, conserving energy; and gas
flow around two bodies over 64 by 64 parcels. It prints per-phase timings and
passes 6,404 assertions in 8.2 seconds. `--contact-benchmark` times the first
and steady 2D physics steps of 1,000 and 4,000 circle colliders, apart and
overlapping, and passes 96 assertions in 1.4 seconds. Compare builds through
`benchmark_trials`.

`--asset-values` runs nine cases on KDL asset text and the native value tree:
typed scalars at the 64-bit limits, slashdash, annotated arrays and source
ranges; reals and floats that read back bit for bit and write plainly;
escaped separators; thirteen malformed documents reported with a line and
column; owned snapshots and subtrees; ordered objects; exact numeric
equality; and JSON that owns its strings. 172 assertions in 0.05 seconds with
ASan/UBSan; in memory, CPU only.

`--ui-document` runs fifteen cases on UI documents and RCSS: composition,
rejected markup, page bodies with imported stylesheets, widgets expanded with
slots, attributes and cycles refused, source lines traced through widgets,
scoping, named events and self bindings, then RCSS nesting, errors by line,
custom properties, line maps through joins and imports read once. 79
assertions in 0.06 seconds with ASan/UBSan; a few files in a temporary folder.

`--text-files` runs three cases: a file watch sees same-size edits, atomic
replacements and removals at most once a second; a text overlay reads in
place of its file however its path is spelled; text edits apply against the
original text. 16 assertions in 0.06 seconds with ASan/UBSan.

`--fmt` runs two cases on the vendored fmt library: positional, typed and
aligned arguments with escaped braces, and the runtime specifier path Format
Text takes, where an out-of-range placeholder renders empty. 12 assertions in
0.05 seconds with ASan/UBSan.

`--particles` runs three cases on particle bursts without a renderer: curves
and one long step matching many short ones, deterministic emission that
rejected input does not disturb, and bursts capped at the particle limit with
finite values that expire each at its own lifetime. 6,259 assertions in 0.07
seconds with ASan/UBSan.

`--png-encoding` runs one case: Cute's PNG encoder, which screenshots use,
declares the 32K zlib window it references and round-trips the pixels. No
window or GPU; 3 assertions in 0.07 seconds with ASan/UBSan.

`--project-document` runs three cases: manifests older than the oldest
upgradable version or newer than this build are refused, a new project opens
with an empty start scene and is never written over, and the language and
material contract fixture projects resolve their own content. 4 assertions in
0.08 seconds with ASan/UBSan; temporary projects only.

`--project-declarations` is one standalone pico compatibility case under the
shared 60-second ceiling. Its four exact
fixtures in `test/fixtures/studio-declarations` are `OutlinerRows.chibi`,
`VirtualList.chibi` and their sidecars from Studio
`07dccf7b5fcfcc879b9d24e95730f4ee1d6a281f` (36,450 bytes total). Each source must
migrate from version 23 to 24 and repeat that step unchanged. Explicit retention
and syntax migrations then produce current-grammar source for parsing. One
successful content transaction must produce exactly the version-24 direct migrated
source.
Both original sidecars are already stale: the public source-binding check must
fail before migration, and afterward their full bytes, historical source hashes
and 104/41 anchors must remain unchanged and valid JSON. This does not claim
valid-bound anchor remapping; the existing
`program_version_23_declarations_and_switch_migrate` case covers that contract.
A second transaction adds one 32-byte malformed source and must reject it with
its actual path, leaving all five input files byte-identical and no `.migrating`
files. The fixture prints each source name before its migration assertion so a
rejected rewrite identifies the original source and current-parser diagnostic.

The original diagnostic at `66448ee5` rejected the OutlinerRows rewrite at
26:75, inside `not(Value: state.Collapsed)`: one failed case, three assertions,
0.062 supervised seconds. Later controls were not reached. The migration now
quotes only historical bare `not` call targets using existing quoted-name
syntax; the current unary grammar is unchanged. Assertions preserve all four
real call arguments. Two fixed in-memory snippets under 1 KiB additionally
check exact rewrite exclusions for comments, strings, member and already-quoted
call targets, plus unchanged current `not(value)` syntax with a unary CST node
and no call node.

The redesigned success-path ceiling is six declaration migration calls, four
retention/syntax migration calls, three explicit current parses, four metadata
checks and two content transactions over four then five input files. Including
the migration implementation, there are at most 63 current/historical syntax
parses (twenty declaration, twelve retention/syntax, three explicit, twelve
successful content and sixteen rejected content) and six simultaneous
content/staging files. This updated workload requires independent exact-source
admission before execution under the unchanged production 60-second ceiling.
There is no authored execution, backend, renderer, child process, network or
additional corpus. The test object consumes only explicitly linked public
`Chibi::project_document`, `Chibi::language_syntax`, `Chibi::authoring` and
`Chibi::json` owners plus the copied pico/fixture headers. The route stays outside
all default aggregates. At `c4a9ed52`, this fixed route passed one case and
60 assertions, exit 0 in 0.082 supervised seconds. That historical run predates
the redesigned migration chain. The current 63-parse bound is source-derived,
not an instrumented count. Already-current `not(value)` keeps the existing
unchanged early return; this is not general version-aware
call/unary disambiguation.

A fresh full compiler/native/desktop SDK from that same source passed independent
package/relocation and current controller/Studio consumer-link verification.
With Studio `d79bb24c`, its separate fail-fast case passed one case/50 assertions
in 0.057 seconds. A separately supervised empty-scene startup requested two
frames at fixed 1/60-second steps and produced a visually checked opaque
1920x1080 capture, exit 0 in 4.529 seconds, using the retained offscreen/software
Mesa configuration. No prohibited syntax/initialization/frame-failure markers
appeared and all 21,668 immutable paths were unchanged. These results do not
cover interaction, hardware-GPU behavior, sanitizers or leak detection. The SDK
uses Clang 19 Release/NDEBUG; the focused pico assertions remain enabled.

`--prototype-documents` runs eight cases on prototypes and scene documents in
memory: a rebase keeps local values over evolving defaults, retained strings
survive clones and renames, detached components spawn absent and reattach,
includes and overrides save deterministically, spawned edits capture as
overrides, and scene documents save reflected values readably, round-trip
entities and physics, and refuse invalid physics names and versions. 192
assertions in 0.08 seconds with ASan/UBSan; no files.

`--reference-index` runs five cases on the authoring reference index: assets
keep their identity by path and key, graphs index every kind of symbol against
the game's reflected packs, prototypes, objects and UI documents are indexed,
calls climb through handlers and detect cycles, and a scene's UI pages bind to
and rename with their scene. 72 assertions in 0.36 seconds with ASan/UBSan.

`--asset-source` runs six cases on asset source text: completion of unset
properties, components, fields and values (in prototype and scene drafts),
navigation into event payloads, reconciliation that keeps authored text, and
parsing incomplete drafts. Canonical component identities resolve completion and
hover, while an unknown qualifier does not resolve the component. The original
five cases passed 505 assertions in 0.32 seconds with ASan/UBSan; the added
identity case requires validation on this candidate.

`--behavior-loader` runs two cases: the application loader prepares a content
root's sources into the authoring world and a prototype preview's live world,
and one broken source refuses the whole collection, names its root and stage
and leaves both worlds on the generation they had; a preview scene launch that
fails destroys its entities before releasing their component override strings.
11 assertions in 0.23 seconds with ASan/UBSan; a temporary folder.

`python3 tools/test_policy.py tool native-runtime` includes two fixed player
Panic-relationship cases: a refused scene change preserves its current instance,
entities, links, globals, scene-flow name and unsaved values, while shutdown
releases its entire owned world, including a disabled external Panic source.
These additions create three in-memory player worlds with at most two live
entities each, without frames, processes, GPU work or files.

Studio's admitted `studio-source-edit` check also covers the production Undo/Redo
dispatch for prototype and relationship source tabs, including typed reopening
of their saved tab snapshots. One temporary file below 65 MiB verifies that the
general asset reader accepts text larger than the prototype collection budget,
while collection rejects it before a second read or parsing. The buffer and file
are released before the two tiny history fixtures. These cases add no rendered
frames to the existing four-frame check and keep its shared 60-second deadline.

`--engine-clock` runs six cases on the engine's 60 Hz fixed clock over a
preview engine and one kinematic body: 30, 60, 120 and 144 Hz displays all run
sixty steps a second with systems at each step's time, hitches and overload are
capped at eight steps and reported, pause, Step, resume and reset hold wall
time, time scale changes how often steps run, display frames interpolate poses
without moving bodies, and an engine without optional modules still steps.
436 assertions in 0.18 seconds with ASan/UBSan.

`--preview-layout` runs two cases on preview graph definitions: moving
automatically arranged nodes leaves a definition saved and applied while moving
a placed node is an unsaved edit, and component access nodes keep their file
entries wherever arrangement puts them. 26 assertions in 0.25 seconds with
ASan/UBSan.

`--fields` runs twenty cases on field graphs compiled to field programs:
deterministic cos/sin, atan2 and exponentials against golden bits, primitives
that bake as they sample, parameters and compile errors, noise and
composition, row evaluation matching scalar samples, repeated inputs,
functions, multiple results, terrain chunks and connectivity, and streaming
tiles that restore and retry failed slices, plus linked-library publication
and rollback, source-library linking and constant parameter defaults, and library
updates that preserve valid and invalid field drafts, and source calls that retain
the named function and authored inputs after functions and parameters reorder.
Two more lower a library calling every field node, each baked bit-identical to
that node alone with the field's seed, sample and parameters, and functions
calling functions before and after them, in another library, and recursively.
The original thirteen cases passed 3,308 assertions in 0.36 seconds with
ASan/UBSan; these historical measurements do not cover the seven added cases.
All twenty passed 3,648 assertions in 2.4 seconds with ASan/UBSan on
2026-10-16.

After `make test-build` and `make build-release BUILD_TARGET=chibi_ctl`,
`python3 tools/test_policy.py tool validate-project` runs its cases against
`build-release/bin/chibi_ctl`. Each scaffolds a temporary
project with `new_project` and checks the `validate_project` reply: a new
project is valid; a UI style error is listed under its asset; a start scene
outside the scene assets is parsed and checked for its prototypes; a broken
start scene inside them is reported once; a UI document named by the
manifest or a scene must exist, and markup that is a folder or a link is
refused; a scene's missing prototype or behavior, and each syntax error in a
source, is one error; a project saved by an earlier build is reported, not
upgraded, and one from a newer build needs that build; the application icon must decode as a PNG and a long project name
needs a title; a prototype that includes another validates, runs in a scene
and keeps its include through `--format`, while a missing include is
reported and the included prototype's behaviors stay its own references; a
prototype is found by the name it declares, even when another file bears that
name or the name could not name a file, `new_asset` refuses a declared name,
two declarations of one name are a conflict, and a file the search cannot read,
or one declaring a name longer than the player takes, fails it; a scene runs as
a scenario in a project that has a material; a scene requested by its file name
runs, and reloads, under its own name; a scenario's controls hold the input
map's actions; a scenario and validation accept a real
Ogg Vorbis sound, but refuse copies whose headers or audio packets players
cannot decode; sounds that fit the player's resource count alone, but not with
the project's other resources, fail validation as they fail compiling; and in a
project whose manifest names another content folder, assets are scaffolded,
validated, run, formatted, found and reported there under their `content/`
paths, and `status` names that folder even once the project is too old to open.
In a compiler build, `import_model` names a model for its file, writes files
that validate and that `--format --check` keeps, and refuses a name that is
no asset name or that a prototype already declares, and a model it cannot
read, writing nothing.
A few short `chibi_ctl` runs, CPU only; the project is removed afterwards.

`python3 tools/test_policy.py tool prototype-expansion` runs, the same way, the
two cases whose projects read past the prototype expansion budget that
preparation shares: a project's prototypes, each under its own load's limit,
and a scene's instances of one prototype tree. Each reads tens of thousands of
small prototype documents, so they have their own selector.

`--source-edit` admits nine independently reviewed cases with fixed small inline
sources and checked edit transactions. They check expression/type validation,
effectful result reuse, lexical capture rejection, statement deletion/reordering,
structural selections, typed values, optional-input reconnection, palette creation,
declaration edits, and comment-box/reroute preservation. The two added cases use
four sources smaller than 200 bytes and twelve fixed edits plus clipboard checks;
the review repairs add one tiny capture counterexample and two tuple selection
requests. No files, runtime frames, processes or GPU work run. The first three-case
snapshot passed one and failed two. Three more admitted cases cover extraction,
inline capture/default handling, signature changes and resolved binding intents
using seven tiny sources and fixed requests. The first five-case integration hit
an invalid-expression parser-node crash; its bounded debugger capture identified
the exact null parent lookup. The guarded producer repair is reviewed and the
nine-case capture is pending. The ninth case uses three small analyses and fresh
projections to check program/path rename and persisted geometry, watches, boxes
and routes. It also checks atomic rejection of a malformed origin table, wrong
expected bytes and a non-name edit. Debugger completion is diagnostic evidence only.

`--source-systems` admits seven isolated pico cases from `test/code_system_runtime.c`
on VM and SLJIT. Fixed supplied-time steps and pumps cover membership, suspension,
spawn cancellation, refreshed component reads, query formatting, reactive bindings,
movement and camera globals. Each fixture has at most 259 entities, and the cases
use no renderer, sleeps or child processes. Its exact dispatcher runs only registry
validation and these cases. The migrated selector is independently reviewed and
admitted for its first supervised capture.

`python3 tools/test_policy.py tool tilemap` selects eight reviewed C cases for
owned grid decoding and cache invalidation, half-open point lookup, exact rotated
rectangle overlap, transformed bounds and native ECS binding lifetimes. Each
grid has at most four cells and no rendering or engine frames run. A decorative
prototype also checks, without gameplay, that its atlas and its normal and height
maps are prepared and that it owns its prepared data, and a map of another size
must fail preparation.

`python3 tools/test_policy.py tool webgpu-staging` runs a C-owned disposable
Chrome process against the browser build of the same tooling dispatcher in
`build-webgpu-test`. It draws one 64-by-64 quad for 120 animation frames, checks
staging-buffer reuse and a 64 MiB peak ceiling, then checks that destroying a
pending readback or its app releases the custom-allocator owner immediately. It
restores the default allocator before draining cancelled callbacks. With an
imported device it recreates the app, delivers a controlled old loss continuation,
and verifies that the new app stays healthy. A real validation error must reach
both CF and the page's listener; a real device destruction must mark the backend
lost. Each callback phase has at most one second, clipped to the shared 55-second
startup/execution deadline inside the production 60-second limit.
The isolated Cute source is selected with `CHIBI_TEST_CUTE_SOURCE`; the comparison
build's `CHIBI_TEST_WEBGPU_BASELINE=ON` retains CF's ASYNCIFY initialization.
Callback wall time excludes the first 30 frames and is diagnostic, not a timing
assertion. This small workload does not establish whole-game performance.

`python3 tools/test_policy.py tool source-native-numeric-queries` selects five
finite C cases on VM and SLJIT. Besides numeric outputs and nested entity query
membership, the rectangle case prepares one declared Boolean component and two
contextually typed `EntityCondition` arrays. Ten fixed transform positions check
fractional overlap, strict rectangle edge contact and half-open point inclusion.
It retains the query snapshot while disabling, removing, restoring and destroying
its single member. The case creates no windows, files, subprocesses or engine
frames and uses at most 16 bounded event dispatches per backend. Independent
review admitted the extension; its supervised run passed three cases and 722
assertions in 1.351 seconds. The room-border rules and player spawn remain
project-owned scenario coverage.
The tilemap case executes both tilemap functions on VM and SLJIT using one
two-cell map, then edits its cells and checks the next dispatch sees the change.

`python3 tools/test_policy.py tool language-headless` selects two independently
reviewed public SDK consumer cases on VM and SLJIT. Each owns one entity and
scalar component, runs five explicit steps, and checks registration ownership,
Begin, Tick, named payloads, bindings, destruction and removal. Preparation uses
`Chibi::authoring`; its source snapshot is freed before execution. The second case
round-trips prepared bytes, retires the producer module, and frees the bytes before
execution. The consumer compiles with only `Chibi::behavior_runtime`. There are no
files, processes or GPU work. Debug Clang 23.1.1 with ASan/UBSan passed two
cases and 106 assertions in 0.635 seconds on 2026-10-07.

`python3 tools/test_policy.py tool language-prepared` starts the existing C tooling
dispatcher with an exact compiler-test producer path. One producer process exports
three small modules and checks repeatable bytes, then exits. A separate runtime-only
process runs four independently reviewed cases: state/events/bindings/imports,
seven missing or incompatible linkage failures, ten malformed wire mutations,
and atomic package loading with reversed records and three rejection cases.
Both execution backends are checked. Input files are capped at 512 KiB and the
metadata file at 96 bytes. Each child has a 10-second cap within the shared
60-second production deadline; generated inputs and logs are removed by the parent.
The redesign adds two fixed one-function specimens on this same route: a declared
scalar BOOL getter with its actual field capability metadata, and a separate
payload-free native-header artifact written after catalog retirement. Each file
is capped at 16 KiB. The consumer keeps the existing scalar offset-corruption
checks and adds one native-header layout-bit corruption checked against its linked
native descriptor. These add no processes, frames, loops over runtime data or
deadline changes. Validation of this addition is pending.
The consumer translation unit sees only exported SDK headers and its fixture;
the executable links no parser, compiler, graph or renderer library. The frozen
candidate passed in 0.701 seconds on 2026-10-07: producer one case/31 assertions,
runtime four cases/184 assertions, and parent one case/five assertions. The producer
used ASan/UBSan; the consumer was a normal Debug runtime-only SDK build.

`--semantic-corpora`, `--prepare-corpora` and `--prepare-corpora-sljit` take
a manifest with one row per project: its content folder, a tab, and a file that
lists every `.chibi` source in it (at most 16 projects, 256 sources and 8 MiB in
total). They analyze, or prepare for bytecode or SLJIT, every listed source
against the engine catalog alone. They are not in a group: no manifest is checked
in, and the repository's fixtures are not complete projects for them, since some
reference events and prototypes they omit or declare their own components. Run
them on extracted game projects and keep the manifest with the evidence.

Managed test executions, including aggregates, have one hard 60-second ceiling.
Known slow and unmeasured commands are quarantined; hardware opt-in cannot clear
it. See [the inventory](test-quarantine.md) for preserved native coverage,
retired script coverage and exact admitted native tuples.

`make test` and `make test-short` run six pure C execution-policy cases through
`tools/test_policy.py`. This default checks execution-policy decisions only.
`make test-tools` runs the complete C tooling group: execution policy,
package/install and configure preservation, room service, publication,
supervisor cleanup/deadlines and validation-planner regressions. Packaging,
publication and room service implementations remain external programs; C tests
own their assertions. Once `make build` has produced `build/reflect`, the
tooling build also runs it against component manifest identity, unchanged and
failed generation, handle pins and JIT identity; without it those cases are
absent, not passed. Select this complete group for affected tooling contracts.
CI runs `make test-tools`; the planner's `--tools` also selects that complete group.
Both modes retain one shared 60-second supervisor deadline. Retired script suites
have not been reproduced completely.

For reflection range metadata, `python3 tools/test_policy.py tool reflect-ranges`
selects two existing C cases with unchanged assertions: numeric units and range
generation, malformed and nonnumeric annotations, unsafe endpoints, distinct
hints/defaults, and failed generation preserving prior outputs. The fixed sources
are below 2 KiB; twelve reflection children retain their existing ten-second
limits inside one shared 60-second supervisor deadline. There are no engine
frames, GPU or network operations. This is an admission for the first supervised
measurement, not a recorded pass. Configure the tooling driver with
`CHIBI_REFLECT_EXECUTABLE` and select its directory with `CHIBI_TOOLS_BUILD_DIR`;
the selector rejects a driver built without reflection. The complete tooling
group retains both cases. This focused check covers reflection range metadata
only and does not establish a complete tooling-group pass.

The language follow-up admits these focused C checks under the same supervisor:

- `tool ecs-query` runs the existing ECS query correctness cases plus bounded
  membership-cache and query-planning regressions. They create at most 256
  entities, reserve at most 2,048 slots, and enumerate at most two 36-by-36-by-36
  relationship products to check the 1 MiB retention limit. They assert invalidation,
  live values, generation safety, partial-iteration behavior, allocation counts and
  work counters. No files, child processes, engine frames or GPU work run.
- `tool language-query-program` selects source-system cases for relationship
  joins, qualified filters, optional targets, multi-target streams, suspension and
  prepared round trips. The SDK cases and `tool source-query-rows` check generation-aware tuple
  state, reload, independent change cursors, same-value publication, busy tasks,
  provider changes, pure helper predicates, value-gate timing, binding gates and
  stateless memory accounting. Builder/prepared rejection cases check unbound
  variables and impure predicates. The complete program selector retains the SDK
  cases; the native group retains the full-engine source-query cases.
  The new runtime fixtures use at most 33 entities and six supplied-time steps
  per backend, without files, children or GPU work.
  The bound query-variable IR specimen adds finite differential, verification,
  prepared, inspection and allocation cases. Its exact bounds, backend contract,
  baseline and coverage limits are recorded in [Bound query-variable lowering](query-binding-lowering.md).
- `tool language-incremental` prepares four tiny source overlays per case and
  checks unchanged-generation reuse, dependency and catalog invalidation, binding
  generations, backend changes and ownership after older collections retire.
  Signature-unit cases cover cross-library contracts, unit-only invalidation and
  owned native metadata after the producing catalog is retired.
  It uses disposable empty source roots and bounded in-process preparations.
- `tool language-scalarize` and `tool language-ranges` compare canonical and
  optimized scalar IR and execute fixed numeric, ownership and control-flow
  counterexamples on the VM, compiled backend and inspection path where applicable.
  Allocation and instruction assertions establish removed work; elapsed time is
  not an optimization oracle. There are no child processes, frames or GPU work.
  The scalarization cases include forward joins, nested union escapes, retained
  native side effects/errors and bounded analysis fallback; real waits, managed
  payloads and inspection remain conservative counterexamples.
  The scalar-component cases compare authored member snapshots against explicit
  scalar getters, including managed-root observation, exact diagnostics and
  same-context recovery. Direct getter lowering removes reference descriptors;
  ordinary member snapshots remain the fallback oracle. See
  [checked scalar reads](scalar-component-reads.md) for the reviewed workload,
  historical measurements, refreshed integration scope and deliberately unrun coverage.
  One combined 12-instruction scalar/query definition adds 20 calls across VM/
  compiled and inspection modes, with live integer/floating/handle values,
  distinct missing-tuple/stale-entity failures and same-context recovery. The
  combined workload requires independent admission before execution.
- `tool wasm-query-program`, `tool wasm-scalarize` and `tool wasm-ranges` run
  those same C cases through the browser target's Node launcher.
- The optimizer microbenchmarks below use the existing C dispatchers, five
  small trials and the same 60-second supervisor. Setup and correctness checks
  stay outside timed loops; no elapsed-time threshold determines a pass.
  `xalloc` requested bytes measure allocation traffic through that allocator,
  not live or peak memory. Compare Release builds with identical flags and
  retain native sanitizer and Wasm correctness coverage separately.
  `tool language-activation-benchmark` runs three source workloads on 32 owners
  for 100 invocations per owner per trial, on VM and the compiled backend.
  `tool language-binding-benchmark` maintains two programs with eight owners
  and a three-binding chain, for 12 iterations per trial, with stable and changed
  membership. `tool language-borrow-benchmark` sums arrays of one and 128
  elements for 32 dispatches per trial on both backends.
  `tool language-incremental-benchmark` prepares four tiny in-memory sources
  once initially, then five times for each of unchanged, body-edit and
  presentation-edit modes; it creates one disposable empty source directory.
  `tool scheduler-benchmark` compares identical registration and warm dispatch
  of 12 systems over 128 passes per trial, plus owned-descriptor, dependency and
  failure checks. These fixtures perform no child-process, engine-frame or GPU
  work. The four language selectors also have `wasm-` variants using the same
  C checks. Initial admissions establish bounded work, not measured speedups.
  The scheduler selector also covers unchanged owned descriptors, callback and
  effect replacement, failure retry and ordering, one recursive dispatch, and a
  129-system oversized-cache fallback. There are eight fixed cases; cached
  allocation assertions and retained scratch accounting complement timings.
- `tool language-binding-schedule` and `tool wasm-binding-schedule` exercise five
  fixed binding-cache cases on both backends: module/owner generation changes,
  peer destruction, caller ordering, cycle rejection before writes, query-gate
  changes and dynamic dependency targets. Cache fallback uses 17 programs and
  1,025 independent constant binding rows, proving cache bounds are not execution
  limits. There are no frames, child processes or GPU work.
  The fifth case adds managed evaluator ownership through actual source bindings:
  a direct string binding, a boolean binding with whole numeric-array/record
  dependency results, and a constant binding with a zero-result dependency
  evaluator. Array pointer equality is guarded by literal true because state
  reads snapshot separately; record equality keeps the final boolean true while
  changed state still requires fresh owned dependencies. Direct operands avoid
  a helper call that would become a single dependency.
  Two fixed documents (62 and 246 bytes) prepare one module and one owner per
  backend. The added case has four parses/analyses, two lowers/finalizations,
  four initializer BYTECODE entries, twelve maintenance calls, six state writes
  and 714 expected assertions on the successful path. There are 48 root
  evaluator calls and no authored helper calls; each maintenance receives the
  existing 10,000 budget. Selected-backend entry counts are 6, 3, 4, 4, 4, 3 per
  backend. All calls reject unexpected backends, inspection fallback and counter
  overflow. Cache snapshots survive replacement of state and poisoned caller
  storage, ECS text has independent ownership, and detached results survive
  program/module/world teardown.
  Across both backends, the complete five-case route expects 5,588 successful
  assertions, 40 parses/analyses, 38 lowers/finalizations, 56 explicit maintenance
  calls and six bootstrap BYTECODE entries. The 48 evaluator calls and four
  initializer entries above are the new case's delta, not whole-route totals;
  the original cases' query/body/failure work remains inside the same deadline.
  Native runs select BYTECODE and SLJIT; the existing Wasm selector retains its
  BYTECODE/Wasm pair. Both expanded routes need independent exact-source workload
  admission before their first run. These are planned counts, not measured
  passes. Run the whole selected route under its unchanged shared production
  60-second deadline, including preparation and cleanup; configuration and build
  timing remain separate. The benchmark workload and aggregate membership are
  unchanged.
- `tool language-activation-cache` and `tool wasm-activation-cache` use five
  cases on both backends to check warm allocation counts, owner/default reset,
  nested tasks, read-only observation, reload, managed payloads, errors, suspended
  work and debugger pause/resume. Each fixture has at most two entities and ten
  supplied-time dispatch/pump/step operations. One cap fixture has 4,096 scalar
  default arguments and a two-instruction body, executed twice, to prove large
  activations still run without retaining oversized storage. No frames, child
  processes or GPU work are involved.
- `tool language-borrow` and `tool wasm-borrow` use six fixed cases: one authored
  read-only array loop with 0, 1 and 128 items; null/empty/mixed-header and failure
  fallbacks; scalar, saved-activation and VM scratch aliasing; mutation/identity/capacity/escape/opaque
  call exclusions; debug and real-wait continuation; and an in-memory prepared
  roundtrip that rederives the optimization and rejects a forged canonical flag
  (two buffers, each below 64 KiB). Native/Wasm normal and
  inspection execution compare exact values, pointer ownership and arena bytes.
  There are no files, frames, child processes or GPU work.
- `tool c-export-alias` executes twelve tiny generated-C I32/I64 DIV/MOD
  fixtures covering operand and result aliases, signed inputs, division by zero
  and overflow, plus one authored union fixture for source pause, debugger detach
  and continuation. One extra verified array function executes eight normal and
  inspection calls, checking guarded borrowing, mixed-header fallback copies and
  an array whose items alias the public activation's saved-value bank.
  Two bound-query-variable functions add 16 calls covering first/last index,
  high-bit/zero handles, missing tuples and same-context recovery with exact
  source/debug metadata, in normal and inspected modes.
  One compiler child builds one temporary shared library from less
  than 128 KiB of generated source, using the configured compiler and C/build-type
  flags, enabled sanitizers and assertions. The existing 10-second child and shared 60-second
  deadlines apply. The existing tooling driver loads the library and compares 120
  normal/inspection executions against the canonical VM and runs two pause/resume
  samples including negative zero, then removes all files.
  Three additional five-instruction scalar-component functions add 132 calls:
  BOOL, high-bit U64, F64 negative zero/NaN payload, ten unavailable-storage
  failures, exact metadata and alternate-world recovery despite stale cached
  columns. These coexist with the two query-variable definitions and sixteen
  query calls below, sharing the same compiler child and source-size bound.
  The combined generated source requires independent admission before execution.
  This native POSIX source-build check requires authoring and is unavailable for Wasm, cross compilation,
  multi-configuration generators and installed-SDK builds; an explicit unavailable
  selector returns failure.
  The default tooling group retains it. No separate executable, frames or GPU work
  are introduced.
- `tool unit-validation` runs the existing single controller unit-diagnostic
  case with its original setup/cleanup. Tiny component/system sources cover
  project/source errors and same-layout unit changes in one retained controller.
  Its original child deadlines and shared 60-second limit remain unchanged;
  it runs no gameplay frames or GPU work.
- `tool unit-fixtures` checks six fixed measured sources from
  `declared-components` and `feature-coverage` against their native and declared
  component catalogs. It limits each executable source to 16 KiB, their total
  to 64 KiB, and the two component declarations to 16 KiB combined. These sources
  use only native and declared component capabilities, so no unrelated scene,
  prototype or executable declarations are needed. No other bodies are analyzed.
  This is semantic-only representative rollout coverage: no lowering, JIT, scenes,
  child processes or all-project traversal. It does not establish that every
  repository or external project compiles.

The call-view/reduction follow-up adds bounded affected-contract checks:

- `tool language-reductions` and `tool wasm-reductions` run four fixed public-SDK
  execution cases on the VM and compiled backend, with at most eight rows, a 4,096-step
  budget and prepared buffers capped at 64 KiB. They cover producer retirement
  before prepared reload, typed ordered folding, wrapping, clamping, unordered
  regrouping, NaN/signed-zero min/max, and whole-log rejection/discard. No files,
  children, frames or GPU work run. A fifth case finalizes three tiny public-IR
  predicate variants (snapshot, raw read and native aggregate input) and checks
  conservative component dependencies without executing rows.
- `tool source-reductions` covers source/engine scheduling, dependent value/change
  filters, failed preparation and publication, actual worker overlap with a
  two-second bounded gate, and debugger pause/resume/reset using fixed supplied
  clocks and at most four owners per regression fixture. The debugger fixture
  has one kinematic physics body and no renderer. Worker tasks are always joined.
  A ninth, one-target case changes a later system's effects while a reducer is
  paused and checks phase restart with fresh contributions and no physics replay.
  A tenth case fails one producer before a later peer pauses and checks that the
  failed batch cancels that peer's log and inspector state without publication.
- `tool source-reduction-benchmark` compares ordinary serial writes and ordered
  or unordered reductions, each serially and with a two-worker executor. Five
  modes each use one target and 256 contributor entities, two systems, one warmup
  and five trials of eight passes (1,024 contributions per pass). It reports
  exact values, published revisions, worker participation, log records, retained
  and peak buffer bytes, allocation traffic and timings. Setup and correctness
  assertions stay outside timers; elapsed time is never a pass threshold.
- `tool language-borrow-benchmark` now measures the direct loop and one/two-level
  ordinary helper calls at the original one/128-item sizes and 32-dispatch,
  five-trial bounds. There are twelve preparations and 1,920 timed dispatches
  total, without files/children/frames/GPU work. The same fixture runs on Wasm.
- The existing borrow ownership selector adds helper forwarding, callback,
  actual suspension, mutation/escape, prepared linking and program-state-header
  aliases. The C-export fixture adds a caller export and callback fallback to
  its one existing shared-library compilation, still capped at 128 KiB source.
- Four additional scheduler cases use three synthetic registrations, a reverse
  inline executor and fixed failures/pauses to check ordered publication,
  prepare-after-commit, discard and resume without replay. They create no worker
  threads and stay in `tool scheduler-benchmark`.
- The component-setter source-presentation case also projects four tiny reduction
  spellings and checks receiver/value origins and reduction-mode cache identity.

These are first-measurement admissions after independent workload review, not
recorded passes. The original cases, assertions, sanitizers and 60-second limits
remain intact. Separate semantic, syntax, formatting and prepared-boundary checks
still cover their affected contracts; these selectors do not imply a full-suite pass.

The execution-storage and row-worker follow-up adds these reviewed bounds:

- Existing `language-query-program` / `wasm-query-program` cases add three-filter,
  nested-helper scratch reuse on four owners over four steps, then one module
  replacement per backend. A failing predicate first warms its cache. Two further
  cases cover update clocks, queued handlers, a delayed continuation and debugger
  retry (eight tiny runs), plus two three-row filtered query refreshes. No real
  waits, files, children, engine frames or GPU work are added.
- Existing activation-cache checks use two ordinary helper levels and add a
  4,096-value host-bank cap variant, two executions per backend. The execution
  selector adds four fixed-struct host calls to check clearing and service
  rebinding. Payload-reuse extensions add one array warmup and two dispatches,
  one managed-string reuse, and one 65,537-byte oversized payload followed by
  two small dispatches per backend. No workload uses an executor or real wait.
  The full program selector retains the query regressions.
- The SDK reduction selector adds four scalar-helper proof cases on each
  backend, with two input rows and one destination per case. A reachable
  numeric helper chain qualifies; component-reading, array-argument and unused
  helpers stay conservative. Eight small compilations and updates use a
  4,096-step budget, without an executor or additional processes. The positive
  case also passes through the existing bounded prepared-artifact buffer. One
  two-row public-IR case per backend preserves mixed-mode serial failure owner
  and source diagnostics without publishing partial results.
- `source-execution-benchmark` measures one program on 16 light rows, 1,024 light
  rows and 1,024 rows with 64 integer-mixing iterations, serially and with a
  requested two-worker executor. An ordered heavy pair complements the unordered
  modes: eight fixtures, one warmup and five trials of four passes each. It
  reports exact results, published revisions, actual row jobs/worker participation,
  warm allocation traffic and idle retained bytes. Timing never determines a pass.
  Workers are joined; there are no files, children, engine frames or GPU work.
- Source-reduction correctness adds stable row-order and failed-dispatch checks,
  inspection/profile fallbacks and metadata-cache invalidation controls for
  component-column relocation and entity generation changes. The scheduler
  fixture checks the singleton row hook and keeps multi-system work on its
  existing executor path.

Configure/build separately and report that time separately from test execution:

```sh
make tools-build
make test
make test-build
make test-native
make test-list
```

| Command | Current coverage |
| --- | --- |
| `test`, `test-short` | Six pure C execution-policy unit cases under one deadline. |
| `test-tools` | Complete C tooling and process integration group under one shared deadline. |
| `test-native` | Every other CPU `test_tools` selector with checked-in inputs under one shared deadline. |
| `test-materials` | The CPU material simulation selectors under one shared deadline. |
| `test-projects` | The project, asset import and Release `chibi_ctl` selectors under one shared deadline. |
| `test-language-all` | The admitted native language aggregate under one shared deadline; standalone selectors are listed separately by `make test-list`. |
| `test-bootstrap` | Stage one compiles the compiler and stage two compiles it to the same bytes, on an unsanitized Release `test_language`; see `docs/package-compiler.md`. |
| `test-recovery` | The Release compiler host recovers the compiler from the artifact `make bootstrap-artifact` wrote, with sources exported from HEAD; see `docs/self-hosting-build.md`. |
| `agent-test` | One admitted selector routed by `tools/agent.py`. |
| `test-list`, `agent-list` | Admission inventory without test execution. |
| `test-fast`, `test-full`, `test-slow`, `test-render`, `test-compiler-parity` | Retired or quarantined, disabled and not passed. |

The `language` and `wasm` groups list their admitted members explicitly in
`tools/test_policy.py`; they do not include every standalone selector. Both
include the six-case row-array construction, alias, rejection, collection and
in-place local array regression (200,000 appends, 100,000 record appends,
aliasing through copies, borrowed arguments, `for` over arrays, record items and
union payloads, on both backends in ordinary and collectable storage) and the
packed storage regressions.
Both also include the two-case Outliner selected-range projection
specimen: twelve-field policy over owned synthetic inputs, filtering/windows,
full-width keys and detached ownership. Its two mechanical text bindings are
test-local; it is not live Studio integration or a production text API.
Other focused record routes remain standalone, as do the dedicated Wasm
formatter and root-state routes. Use `make test-list` to inspect the groups
and standalone routes; registering a route alone does not add it to a group.

The bounded Chrome C driver also includes counted-row construction and Outliner
projection among its ten selectors. Its internal 55-second and production
60-second deadlines are unchanged.

Timeout is failure. Nested managed wrappers inherit the deadline; no child gets
a fresh aggregate budget. Multi-goal test execution is unadmitted. Direct native
binaries, raw CMake checks and controller captures do not universally enforce
the ceiling; do not use them to bypass it or rebuild slow suites from chunks.
The supervisor verifies cleanup discovery before test work. Linux subreaping
contains escaped sessions; arbitrary already-detached descendants are not proven
contained on macOS. Platform validation must report what actually ran.

For repeated admitted C workloads, `benchmark_trials` accepts three or five trials
and a total 3–60-second timeout including cleanup. It preserves workload output
and reports elapsed time. The Make benchmark aggregate remains quarantined.
Retired Python drivers' generated-C comparisons and report formats were not
migrated individually. Research/review command transcripts, measurements and
hashes are historical records of their revisions, not current reproduction
instructions or passes for this cleanup.

CI remains partial coverage. Report the checks that actually ran. Applicable
server-required checks, independent review, explicit holds and approval rules
remain binding.

## Package execution checks

`python3 tools/test_policy.py tool language-package-execution` selects seven
fixed C cases in `test_language`: sibling calls and mutual recursion on bytecode
and SLJIT, stable exported identities, document-local errors, duplicate exports,
declaration discovery, source ownership and sibling records/unions/constants.
The workload has at most three documents per snapshot, twelve fixed texts below
1 KiB each, 32 parses, 15 analyses, twelve lowering attempts, eight backend
finalizations and sixteen direct calls with instruction budgets of 2,000. It
performs no filesystem, subprocess, world or GPU work. Build separately and run
under one shared 60-second production deadline. The independent package graph
and syntax routes remain unchanged; this focused route is not in the aggregate.

`python3 tools/test_policy.py tool language-package-resolution` selects eight
fixed C cases for explicit roots, equivalent paths, file-local aliases, blank
imports, located invalid paths and strings, duplicates, cycles, stable keys and
owned input lifetimes. There are 24 snapshot constructions, at most 56 parses and
34 import declarations, with up to five files and four imports per snapshot and
three simultaneous owners. Each source is below 1 KiB. The route performs no
filesystem, process, authored execution or GPU work. Build separately and use
one shared 60-second deadline; this focused route is also outside the aggregate.

`python3 tools/test_policy.py tool language-package-imports` selects twelve fixed
C cases for dependency execution, alias isolation, self imports, owned exported
records/unions/constants, stable identities, incremental invalidation, retained
generations, constrained generics and constant defaults. It also checks owned
union payload defaults and malformed public export bindings. Cached closures
track sibling addition, path-only renames and removal across the dependency
chain while an unrelated package is reused; failed generations publish no module
or exports, are never cache hits, and repair replaces them while a separately
retained valid module keeps working. The workload has 59 preparations, 210
parses, 113 import declarations, at most 189 package visits before reuse, and 48
direct calls with instruction budgets of 2,000.
One catalog snapshot and one additional parse/nine semantic-only analyses cover
the metadata cases. At most four owners and five source files per snapshot are
live; each source is below 1 KiB. No filesystem, process or GPU work occurs.
Build separately and run under one shared 60-second deadline. This focused
route is outside the language aggregate.

## Disk package-source checks

`python3 tools/test_policy.py tool language-package-files` selects eight fixed
C cases on native Linux and macOS. It reads disposable source forests through
`source_package_snapshot_load`, then prepares siblings, relative dependencies
and an explicit `core:` collection on bytecode and SLJIT. Cases check relocation
identities, unchanged reuse and dependent invalidation, missing-import source
ranges, descendant symlink rejection, explicit root symlink canonicalization,
duplicate physical roots and scanning overlapping roots once. An explicit root
under a hidden ancestor remains visible. NUL, an oversized regular file and a
`.chibi` FIFO are rejected; the FIFO has no writer.

The finite workload has 22 loader calls, 12 preparations, at most 63 parses and
30 imports, at most 36 package visits before reuse, and ten direct calls with
instruction budget 2,000. Each loaded forest has four source files and at most
20 visited directory entries, including dot entries. Eight disposable temporary directories contain
at most 19 recorded cleanup paths per case; cleanup uses only those known paths
and never follows links. Ordinary sources are below 1 KiB. One sparse oversized
file is exactly 512 KiB plus one byte; one FIFO and five symlinks are created.
There are no subprocesses, network operations, engine frames or GPU work.

Build separately and execute once under the production supervisor's shared
60-second deadline after independent source and workload admission. This route
is outside the language aggregate. Windows retains the explicit unsupported
filesystem adapter; buffer-based package APIs are unaffected. Source registration
records no execution result.

`python3 tools/test_policy.py tool language-package-core` selects seven fixed
C cases using the actual shipped `core:strings` source and pure text/array packs.
It covers unsigned bytes, slice bounds and ownership, lexical ordering, ASCII
identifiers, basename extraction, the byte procedures ported from Odin's
`core:strings` against Odin's documented results and edge cases (search,
counting, case, trimming, cutting, repetition, splitting, joining, replacement
and partition), generic native arrays, binding-generation invalidation and
retained modules on bytecode and SLJIT. Eighteen preparations supply 36 files
and 18 imports, with at most 36 package visits before reuse and 378 direct
calls, each with a 10,000-instruction budget. At most three owners and
three-element arrays are live; core source is below 16 KiB and the app below
4 KiB. There is no filesystem, process, service or GPU work. Installed consumers
embed the installed core file through `Chibi_CORE_SOURCE_DIR`. Build separately;
independently review before the first run under one shared 60-second deadline.
This focused route is outside the language aggregate.

`python3 tools/test_policy.py tool language-docs-markdown` selects six fixed C
cases for `chibi doc`'s authored generator: one preparation of the shipped
`markdown`, `site`, `reference` and `grammar` packages with `core:strings` per
case, rendering blocks, tables and site links, laying out navigation and
generating a package reference and a grammar page on bytecode and SLJIT, each
call under a 2,000,000-instruction budget. There is no filesystem, process, service or
GPU work. This focused route is outside the language aggregate.

`python3 tools/test_policy.py tool language-package-tests` selects two fixed C
cases for the shared runner behind `chibi test`. One preparation per backend has
five files: the shipped `core:strings` source, a collection package with a test
that must not be found, a package at the project root and a two-file project
package, with eight tests. They cover discovery order, names and positions,
passes, a failed `expect` reported inside its helper, an out-of-range read, an
exhausted 100,000-instruction budget, a wait and a task start on bytecode and
SLJIT. There is no filesystem, process, service or GPU work. This focused route
is outside the language aggregate.

`python3 tools/test_policy.py tool language-package-graph-chibi` reuses the
existing 23 C graph cases for differential comparison with the authored compiler
component. Two package owners prepare the actual graph/core sources once for
bytecode and SLJIT (four source parses). The corpus makes 28 graph constructions,
so there are 56 authored Builds and 348 bounded public-query calls, with 684
packed File and 590 Import rows across both backends. At most 256 files and 255
imports are supplied in a case. The two maximum Builds allow 50 million
instructions each; the other 402 calls allow 100,000 each. Authored edges are
sparse, scalar arrays contain at most 256 elements, and per-call arenas retire
between invocations. One host observation matrix uses at most 256 squared ints.
The fixture compares complete memberships, order, dependency cells, bindings,
error fields and cycle indices against C, plus public queries. It performs no
filesystem, process, service or GPU work. Build separately and independently
review the exact workload before its first run under one shared 60-second
ceiling. This focused route is outside the language aggregate.

`python3 tools/test_policy.py tool language-syntax-chibi` compares authored
diagnostic traversal with the C parser using fourteen shared source texts and
two independent edits. Each edit compares the original, incremental and fresh
tree: twenty raw Tree-sitter trees and twenty C oracle snapshots produce
40 authored calls across bytecode and SLJIT. Two retained owners prepare the
actual diagnostics and core sources once per backend (four source parses).
Each call allows one million instructions; source inputs are at most 2 KiB,
trees at most 1,024 nodes and depth 128. The adapter transports all public
nodes, including anonymous children and ERROR descendants; Chibi selects
diagnostics and controls subtree pruning. Every diagnostic field and message
is compared after producer storage retires. This route uses no filesystem,
process, service or GPU work. Build separately and independently review the
exact workload before first execution under one shared 60-second ceiling.
It remains outside the language aggregate.

`python3 tools/test_policy.py tool language-source-tokenizer-chibi` compares
the authored tokenizer (`docs/source-tokenizer.md`) with Tree-sitter. It reads
every `.chibi` file below `packages/`, `test/` and `tools/` at run time, about
145 files under 1 MiB in all, and tokenizes each once on SLJIT; every token's
kind, span, row and column must match the leaves of the cursor walk
`compiler_syntax_parse` makes. The scanner's automatic terminators are hidden
from that walk, so each is read from a node that ends after its last child, or
after a prototype's field that no `,` or `;` follows. Files Tree-sitter
rejects, two legacy fixtures today, compare leaves only, and a `>` leaf
directly followed by `>` or `=` matches one `>>` or `>=`. Seventeen fixed
sources cover forms the corpus lacks, such as attributes, keywords as names,
bodies opened on the next line and prototype fields, and ten hand-written
cases cover terminators directly after a token and Odin's errors with their
offsets; both run on bytecode and SLJIT. One owner per backend prepares the
package, and calls are capped at 64 steps per byte. There is no process,
service or GPU work. The route took about 5 seconds in a local sanitizer build
and is outside the language aggregate.

The fixed syntax corpus exercises visible missing tokens, ERROR subtrees and
UTF-8 byte columns after CRLF. It reports hidden-recovery diagnostic counts but
does not require a witness: the current parser selects visible missing tokens
for these inputs. The authored hidden-error rule matches the retained C code;
this corpus does not establish execution coverage for that branch.

`python3 tools/test_policy.py tool language-package-syntax-chibi` uses
the existing eleven extraction cases and all 32 extraction attempts. It adds
32 independent raw parses and 64 authored `Extract` invocations across both
backends, each capped at one million instructions including its diagnostic
traversal. Two retained owners prepare three actual authored sources each.
Fixture texts remain below 256 bytes; raw trees are capped at 1,024 nodes and
depth 128. Complete field names accompany raw nodes, with no native selection
of package headers or imports. Results compare header, import, path and alias
ranges, alias presence, names, source order and diagnostics after producer and
input storage retire. This standalone route has no runtime files, children,
services or GPU work; independently review it before first execution, build
separately and use one shared 60-second ceiling.

`python3 tools/test_policy.py tool language-package-paths-chibi` selects three
cases with 26 fixed lexical requests: twelve existing resolver inputs and
fourteen boundary cases. The unchanged C helper supplies 26 oracle results;
52 authored calls across bytecode and SLJIT allow one million instructions each.
Two retained owners prepare the actual Paths and core sources once per backend
(four source parses). Four fixed input buffers are at most 4,097 bytes including
the terminator. Cases cover the path and joined-path limits before collapse,
long segments, root boundaries, intermediate escapes, absent inputs and absolute
paths ignoring the unused base. Returned strings are inspected after producer
and input storage retire; retained 4,095-byte results survive subsequent invalid
calls and owner teardown. Used bases and floors are canonical absolute paths.
This corpus has at most four segments per request; it does not establish
performance at the maximum segment count. The route has no runtime files,
children, services or GPU work. Independently review before first execution,
build separately and use one shared 60-second ceiling outside the aggregate.

`python3 tools/test_policy.py tool language-package-routes-chibi` compares
thirteen collection-root requests, eleven file routes and sixteen import routes.
Each file/import request also builds its roots independently in Chibi. The
workload totals 67 C oracle calls and 134 authored calls across bytecode and
SLJIT, capped at one million instructions per call. Two retained owners prepare
four actual sources each (eight source parses). Inputs are at most 128 bytes,
with at most three roots and seventeen directory segments. Fixed cases cover
root validation and both duplicate errors, longest-root selection, relocated
keys, depth boundaries, collection routing, Unicode and invalid/NUL JSON strings.
A private yyjson binding supplies decoded values and byte lengths; Chibi owns
all routing and diagnostic decisions. Success results and exact first errors
are compared; the C helper's partial root prefix on rejection is not part of
the authored rejection payload. Results outlive producer and input storage,
and retained root/file/import outputs also survive owner teardown. There are
no runtime files, children, services or GPU operations. Independently review
the fixed workload before first execution, build separately and use one shared
60-second ceiling. This route remains outside the language aggregate.

`python3 tools/test_policy.py tool language-package-snapshot-chibi` reuses the
eight resolver cases and adds eight validation cases: 46 snapshots and
92 authored calls across bytecode and SLJIT, capped at three million instructions
per call. Two retained owners prepare eight actual sources each, each source
smaller than 16 KiB (sixteen source parses). The cases offer 72 files total,
bounding C parses at 72 and private raw parser calls at 144. Requests have at
most five files and four imports; actual
source buffers are smaller than 1 KiB and raw trees have at most 1,024 nodes.
The explicit invalid-path case declares `SIZE_MAX` bytes against a tiny buffer;
both loaders must reject its path before accessing source bytes. It does not
allocate or parse that declared size. Other precedence cases cover file parsing
before later file routing, all-file parsing before import routing, and file
routing before NUL/UTF-8 checks. Three tables cover valid multibyte comments,
five invalid UTF-8 classes, missing source bytes, oversized declarations, invalid
option counts/pointers and an empty snapshot. Each authored collection array
also contains one invalid trailing row, which its declared count must exclude.
Exact first errors, retained document/import prefixes, raw syntax and graph
observations are compared. Input and invocation
storage retire before detached result inspection; retained valid and invalid
results survive owner teardown. Native bindings supply raw Tree-sitter nodes
and external JSON decoding only. There are no runtime files, children, services
or GPU operations. Independently review the fixed workload before its first
execution, build separately and use one shared 60-second ceiling outside the
language aggregate. Comparison alone uses 39 C root setups, at most 72 file
routes and at most 72 header/import extractions from existing trees, adding no
parses. Each retained C tree is walked three times, with depth bounded at 128.
The fixed corpus does not exercise the 8 MiB aggregate source limit or the
4,096-import limit at full size.

The private raw-facts extension appends Tree-sitter `is_named` and `is_extra`
Boolean columns without changing node order, ranges, fields or topology.
`SnapshotNode` retains both flags; `Document.syntax_root` is a rebased syntax
index, or -1 before parsing, independent of its routing `root`. Every retained
node and document is compared to C, including anonymous tokens, comments/extras,
ERROR nodes and missing tokens. The one new 50-byte unclosed-body source adds
one C snapshot and two authored calls (one additional parse per implementation).
Category assertions require both named values, both extra values, an ERROR and a
missing node. Existing error precedence and ownership assertions remain active.

Snapshot.chibi is 15,704 bytes, below its unchanged strict 16,384-byte cap.
The bridge adds two arrays per parse, each with one `pin_value` per raw node;
the retained record adds two Boolean fields per node and one integer per document.
No packages, imports, node limits or instruction budgets are added. The missing
token case uses `return (1; }`, which the pinned grammar repairs with a MISSING
`)`, followed by an unterminated declaration it recovers as ERROR.

`python3 tools/test_policy.py tool language-constant-proof-chibi` compares active
constant admissibility using 36 fixed IR requests and two requests copied from
real analyzer output. Three pico cases run 38 C proofs and 76 authored calls
across bytecode and SLJIT, with two million instructions per call. Two retained
owners prepare the actual IR and Proof packages (four source parses, each source
smaller than 16 KiB); two short analyzer fixtures add two parses. Requests contain
at most 257 expressions, 32 symbols and four aggregate arguments, with at most
256 visited proof nodes. Step inputs range from -1 to 1,024; selected requests
omit step accounting while retaining the depth bound.

The adapter copies expression, symbol, type, effect and argument facts without
computing policy. Acceptance, remaining steps and every successful cache depth
must match C. Cases cover scalar restrictions, effects, rejected calls, symbols,
ordered child failures, shared DAG reuse at different depths, depth boundaries
and a cycle without a step budget. Input and producer storage retire before
comparison, and retained results survive owner teardown. The fixture does not
exercise the production 65,536-visit limit at full size. There are no runtime
files, services, children or GPU operations. Independently review before the
first run, build separately and use one shared 60-second ceiling outside the
language aggregate.

`python3 tools/test_policy.py tool language-constant-scalar-chibi` compares active
scalar folding using 70 fixed requests and 140 authored calls across bytecode and
SLJIT. Three pico cases prepare the four actual compiler packages once per
backend (eight source parses, each below 64 KiB). Each call permits two million
instructions and contains at most seven expressions, one symbol and no aggregate
arguments. The C oracle runs admissibility followed by the extracted active
evaluator; the authored implementation uses separate proof and evaluation
budgets, preserving the production limit of 65,536 for each.

The corpus covers all thirteen physical scalar variants, exact floating-point
bits, numeric conversions, integer wrap and division failures, masked shifts,
result storage and float rounding, string presence, symbols, promotion and
Boolean short-circuiting after full admissibility. Inputs retire before result
inspection. Explicit arena checks verify detached union and string ownership;
retained name and null-text results survive both owner teardowns. The repeated
DAG workload needed to exhaust 65,536 evaluations is not included or admitted.
There are no runtime files, services, children or GPU operations. Independently
review before the first run, build separately and use one shared 60-second
ceiling outside the language aggregate.

The folding interface consumes type-correct analyzer facts: literals carry their
declared storage, comparisons and logical negation produce Boolean values, and
integer and floating operators retain their result families. Admissibility does
not establish these type invariants. The corpus also checks result-width changes
within a family; malformed cross-family C union reinterpretation is outside this
private interface's contract.

`python3 tools/test_policy.py tool language-scalar-lowering-chibi` selects one
C/pico_unit case in the existing `test_language --scalar-lowering-chibi`
dispatcher. It compares an authored Bool/I32 value-evaluator lowering component
with the production native emitter before optimization. It passes 1 case /
33,382 assertions in 3.2s on macOS and is outside every native and Wasm aggregate.

One embedded `tools/compiler/scalar_lowering/Lower.chibi` source, below 32 KiB,
is prepared once for BYTECODE and once for SLJIT. Those two preparations execute
31 named-constant bootstrap initializers each: 62 additional BYTECODE VM entries
with the existing 100,000 context-budget argument. Source preparation and these
initializer runs are part of the supervised workload, not configuration/build.
Eight fixed raw typed-IR requests per backend make 16 top-level authored `Lower`
calls. Each receives a 500,000 context budget, which child activations inherit
and reset; neither that argument nor 16 times it is an aggregate work ceiling.

Six accepted requests cover shared arithmetic/bitwise trees, signed comparisons,
NOT and identity conversions, guarded AND/OR division, ordered Bool/I32 results,
and nested short circuits. Two requests reject an optional type and a call
expression. Raw expression counts are 16, 9, 8, 8, 4, 11, 1 and 1; accepted
visited-instance counts are 22, 13, 9, 9, 6 and 15. Canonical instruction counts
are 32, 26, 19, 18, 16 and 32; slot counts are 28, 18, 12, 12, 12 and 18.
The component caps types/expressions/literals at 24, symbols/variables/results
at two, IDs at four, ranges at 25 and document paths at three (240 bytes each).
Accepted traversal allocates exactly 257 frame rows and permits at most 256
visits/instructions, with no recursive helper call path or generated corpus.

The six accepted requests across both backends and authored/reference routes
produce 24 pre-finish canonical plans and 24 prepared serialization round trips.
Each loaded definition runs two fixed state vectors: 48 target evaluator calls,
24 authored-plan and 24 reference. The generated target bodies are call-free
and receive budget 128. Four fixed AND-plan decoder mutations per backend add
eight decoder-only rejections, with no extra authored or target invocation.
The fixture requires actual SLJIT availability and checks backend entry counters;
there is no Wasm fallback. Input arenas, compiler-package owners, plan/holder
storage and serialized bytes retire before the corresponding retained outputs
or loaded definitions are inspected or run. Pico teardown releases partial work.

The only new execution-library code is a private bounded plan decoder and builder
materializer. The private C oracle shares the existing native value-evaluator
emitter, so its extraction also touches active production query predicates,
binding values and dependency evaluators. Independent integration review must
cover those callers, including managed results and zero-result dependencies;
the new scalar-only differential does not establish that broader coverage.
The oracle header remains private and is not installed as an SDK contract.

There are no workload files, children, services, network requests, engine frames,
GPU operations, sleeps or benchmark loops. Run the complete fixed fixture under
one unchanged production 60-second supervisor deadline,
including preparation, initializers, calls, materialization, round trips and
cleanup. Build/configuration timing stays separate. A timeout does not authorize
splitting the workload. Native producer verification, optimization, finalization,
liveness/borrowing/runtime rewriting, parallel proof, definition snapshots,
module publication, prepared serialization and backend emission remain required.
The shared emitter's managed binding and dependency callers pass the five-case
`language-binding-schedule` and `language-binding-benchmark` routes on native and
Wasm builds. This evaluator component is not a completed source compiler,
stage-one/stage-two self-build or compiler-free normal build/link path.

`python3 tools/test_policy.py tool language-lexical-binding-chibi` compares
lexical lookup, declaration admission and stable identity construction. Three
pico cases run 40 requests (24 fixed tables and 16 requests from four real
analyses) through C and both authored backends: 80 authored calls, each capped at
two million instructions. Two owners prepare Binding, Diagnostics and Strings
once each; those six parses and four analyzer inputs total ten parses.

Each request contains at most 32 symbols, eight scopes, four functions and two
documents. Names are at most 220 bytes, identities and document keys 128 bytes,
and declaration results contain at most three diagnostics. The adapter copies
raw scopes, symbols, functions and document keys. Lookup precedence, parameter
replacement, package namespace rules, exact uint64 identities, diagnostic order,
ranges and 191-byte truncation must match. Fixed expected hashes and message
prefixes supplement differential comparisons. Input and producer storage retire
before comparison; retained declaration results survive both owner teardowns.
There are no runtime files, services, children or GPU operations. Independently
review before the first run, build separately and use one shared 60-second
ceiling outside the language aggregate.

`python3 tools/test_policy.py tool language-nominal-binding-chibi` compares
record and union header admission and nominal metadata publication. Three pico
cases run twelve raw requests and four requests from real analyzed headers:
32 authored calls across bytecode and SLJIT, each capped at 100,000 instructions.
Two owners prepare the four compiler sources once each (all below 16 KiB), and
four analyzed sources below 256 bytes add four parses, for twelve total.

Inputs contain at most one symbol, one function, two scopes and two documents;
strings are below 128 bytes. Cases compare placement rejection, source origin,
record/union storage guards, exact uint64 IDs, inherited type metadata, catalog
names and ordered diagnostics. Actual legacy and package headers supplement raw
facts. The adapter copies input strings before execution and frees that storage
before comparison. Retained published metadata and three diagnostics survive
both owner teardowns. There are no runtime files, children, services or GPU
operations. Independently review before the first run, build separately and use
one shared 60-second ceiling outside the language aggregate.

`python3 tools/test_policy.py tool language-named-binding-chibi` compares ordinary
named annotation binding and catalog/import type lookup. Three pico cases run
28 fixed requests plus eight requests from four analyzed annotations: 72 authored
calls across bytecode and SLJIT, each capped at two million instructions. Two
owners prepare four compiler sources each, all below 32 KiB. The four analyzed
sources add four parses, for twelve total; three quoted fallback requests invoke
the existing private JSON codec six times.

Per-request caps are 32 symbols, eight scopes, four functions, two documents,
eight catalog entries, three imports/exports, four uses, eight raw tokens,
64 semantic types and 128 exact uint64 IDs. Names are at most 220 bytes, fallback
text 256 bytes; fact trees contain at most 128 nodes at depth sixteen. The adapter
copies raw facts, including catalog spellings and export spans. The authored
implementation decides builtin/lexical precedence, aliases, fuzzy ambiguity,
exact matches, entity identity, modifiers, optional boxing and type-use records.
Comparisons cover full IDs and ordered diagnostics; fixed lookup indices and
191-byte truncation witnesses supplement C parity. Detached results and retained
generic/diagnostic results survive input and owner retirement. There are no
runtime files, children, services or GPU operations. Independently review before
the first run, build separately and use one shared 60-second ceiling outside the
language aggregate.

`python3 tools/test_policy.py tool language-type-constructors-chibi` compares
structural union, task and await construction. Three pico cases run sixteen raw
requests and four requests from actual analyzer facts, with forty authored calls
across bytecode and SLJIT capped at 200,000 instructions each. Five compiler
sources per backend, each below 32 KiB, plus four fact sources below 256 bytes
total fourteen parses. The imported named-binding package requires the private
JSON binding during preparation, but these entries make no JSON calls.

Inputs contain at most 64 types, 128 IDs, eight definitions/variants, two
capabilities, four functions, 32 symbols, eight result indices and four members.
Outputs contain at most four variants, two fields and one diagnostic. Twenty C
plans and twenty-one materialization attempts produce nineteen snapshots,
including an extra metadata ownership witness. Fixed checks cover unsigned tag
ordering across high and low bits, first-match deduplication, cache hits, native
empty-result precedence, diagnostics and copied fields/defaults. Retained outputs survive source and
module retirement. Sorting uses at most six comparisons of 64 scalar bit steps
each. There are no runtime files, children, services or GPU work.
Independently review before the first run, build separately and use one shared
60-second ceiling outside the language aggregate.

`python3 tools/test_policy.py tool language-type-relations-chibi` selects four
C/pico_unit cases for the five pure catalog type relations. The finite tables
contain 18 equality, 15 numeric, 36 assignment, 33 catalog-assignment and nine
canonicalization requests: 111 requests, each once on bytecode and SLJIT, for
222 direct authored calls with a supplied budget argument of 50,000. Outlined
and inline child activations inherit/reset that budget; it is not a complete
invocation or aggregate instruction ceiling. Five fixed compiler sources below
32 KiB per backend produce ten preparation parses. Raw maxima are
five catalog definitions, eight variants/type rows, four exact uint64 IDs and
three alias links; there are no generated combinations or extra analyzed inputs.

C parity and fixed answers cover task wildcard direction, boxed optional and
error/literal precedence, numeric/generic/reference/name/string rules, full-width
IDs, alias boundary/cycle behavior, and union tag inclusion, tagged exclusion and
plain/literal membership. Inputs retire before comparison, and one canonicalized
cycle result per backend survives compiler-owner destruction. The imported
named-binding closure uses the existing private JSON binding during preparation;
these entries make no JSON calls. There are no workload files, children, host
services, frames or GPU work. Independently review the exact wired candidate
before first execution, build separately and use one shared 60-second production
deadline. This route stays outside aggregates. Source registration records no
measured pass, authored compiler dispatch or stage-one bootstrap.

`python3 tools/test_policy.py tool language-unit-algebra-chibi` compares unit
parsing, dimension equality, multiplication/division and formatting. Four pico
cases run 24 Parse, five Same, ten Product and nine Format requests, totaling
96 authored calls across bytecode and SLJIT at two million instructions each.
Units and CoreStrings are prepared once per backend: four preparation parses.
Inputs contain at most eight terms and 240 bytes per input or name; format
capacities range from one to 192 bytes including the C terminator.

Each C result has an independent expected witness. Equality, product and format
inputs are raw algebra values, independent of C parsing. Cases cover missing
and empty annotations, integer powers, overflow before cancellation, term order,
sticky denominators, superscript/dot UTF-8 truncation and tiny output buffers.
Detached results and a retained unit/string survive input and owner retirement.
There are no runtime files, children, services or GPU work. Independently review
before the first run, build separately and use one shared 60-second ceiling
outside the language aggregate.

`python3 tools/test_policy.py tool language-signature-reader-chibi` compares raw
annotation and signature reading. Four pico cases run seventeen Type and eight
Signature requests, totaling 50 authored calls across bytecode and SLJIT at
500,000 instructions each. Seven compiler sources per backend, each below
48 KiB, and 25 raw Tree-sitter inputs total 39 parses. Quoted names make four
private JSON callbacks. The original 24 sources are below 256 bytes with at most
256 nodes at depth 32. One fixed 65-array annotation stays below 512 bytes,
512 nodes and depth 256, checking helper call depth beyond the default limit.
Raw fields, named/extra flags and source text are preserved.

Input tables contain two scopes, two symbols, one function, two documents, three
catalog definitions, two variants and one import/export; a chained returned
environment adds one definition. Pools hold at most 64 types and 128 IDs, with
at most four new definitions/publications/uses/diagnostics. Comparisons include
constructor selections and both returned catalog views. One task result feeds a
later array annotation directly from the authored returned environment. Four
retained readings produce eight before/after serialization projections, each
below 128 KiB, checking ownership after producer retirement. No runtime files,
children, services or GPU work occur. Independently review before the first run,
build separately and use one shared 60-second ceiling outside the aggregate.

`python3 tools/test_policy.py tool language-callable-headers-chibi` compares
callable header registration from raw syntax and analyzer tables. Two pico cases
run fifteen requests on both backends: 30 Register calls at two million
instructions each. Eight sources per backend (Headers below 64 KiB, dependencies
below 48 KiB) and fifteen raw trees total 31 parses, with four private JSON calls.
Inputs are below 512 bytes with at most 256 nodes at depth 32.

Inputs have one scope, at most one symbol and no functions or catalog definitions.
Outputs have at most ten symbols, two scopes, three functions, two defaults, one
body and four uses/diagnostics; this corpus publishes no constructor definitions.
Membership banks hold sixteen cells, with 64 types and 128 IDs. Comparisons cover
registration changes, final tables, active membership spans, pending syntax
references and both refreshed reader views. A complete measured header survives
input, syntax and module retirement. No runtime files, children, services or GPU
work occur. Independently review before the first run, build separately and use
one shared 60-second ceiling outside the language aggregate.

`python3 tools/test_policy.py tool language-union-members-chibi` selects three
C/pico_unit cases: sixteen source requests and eight raw cycle-validation
requests, each on bytecode and SLJIT, for 48 authored calls and 32 parses.
Source calls supply 2,000,000 instructions; raw validation supplies 16,000,000.
Both budgets are inherited/reset per activation, including outlined and inline
children; neither is a whole-call or corpus instruction ceiling. Eight compiler
sources per backend are below 48 KiB. Sixteen fact sources are at most 512 bytes,
256 syntax nodes and depth 32; initial source tables hold at most two symbols
and definitions, one scope and no functions, imports or capabilities.

The fixed raw maximum is 257 definitions/fields, two variants, four roots,
520 types and 1,024 IDs, with at most 66,049 nominal comparisons, 258 traversal
calls and 514 persistent state writes. Cases preserve the 256-definition success
boundary, 257-definition failure and cycle-before-depth ordering. Comparisons
cover ordered diagnostics, uses, constructor publication/reuse, full-width tags,
embedded NUL and results retained after input/module retirement. Positive native
task capability-result metadata is uncovered, and this corpus does not establish
all import/native/multi-document combinations or arbitrary state continuation.
The validator remains recursive; source review is not evidence that the runtime
executes its depth-boundary workload. The normal union analyzer is unchanged.

`python3 tools/test_policy.py tool language-effect-analysis-chibi` selects four
C/pico_unit cases: 24 raw Analyze graphs, six Order graphs and four analyzed
source replays, for 34 requests and 68 authored calls across bytecode and SLJIT.
The supplied 500,000 instruction budget is inherited/reset per activation,
not a whole-call or corpus instruction ceiling. Three compiler sources per
backend below 32 KiB plus four fact sources below 512 bytes total ten parses.
Raw maxima are 24 expressions except one fixed 85-expression graph, 16 statements,
six functions, eight symbols/scopes, two queries, four bindings, eight arguments,
16 children, six capabilities, three catalog types and sixteen read/write IDs.
Actual facts cap expressions/statements at 64, functions at eight, symbols at 32,
scopes at sixteen, queries at four and bindings at eight; shared bounds are
128 IDs, 256 ranges, 32 diagnostics and 240 text bytes.

The corpus retains the function-count-plus-one propagation stop, dependency
cycles, task masks, diagnostic phases, stable binding order and owned outputs.
Raw seeds establish initial propagation; analyzed replays already contain final
effects and only establish schema, diagnostics and ownership. The two existing
forward-edge graphs assert the finite propagation stop, and actual diagnostic
matching is an ordered subsequence without reusing matches. The reviewed
explicit-int generic inference correction is included in `Effects.chibi`.
The normal effect analyzer and binding finalizer are unchanged.

These union/effects routes add private C reference helpers and exact authored
source embeddings to the existing language driver; there is no active production
adapter, new executable, public package or language syntax. No runtime files,
workload children, network, sleeps, services, engine frames or GPU work occur.
Both full routes stay outside all aggregates. Independently review the exact
combined source and finite workloads before execution on a new tree, each under
one unchanged production 60-second deadline including preparation and cleanup;
configuration/build timing is separate. Registration alone is not a measured
pass, active authored compiler dispatch or complete compiler bootstrap; the
landed-tree evidence and new integrated-tree requirements are recorded below.

`python3 tools/test_policy.py tool language-record-members-chibi` is a standalone
source-only differential route for record declaration selection and one-member
name/type discovery. Five fixed C/pico_unit cases make 26 requests and 52 authored
calls across bytecode and SLJIT, with 32 parses and four runtime JSON decodes.
Each call supplies budget 500,000, which outlined and inline child activations
inherit/reset. This is not a shared invocation or corpus instruction ceiling.

Eight compiler sources per backend are below 48 KiB each. Sixteen fact-source
parses use inputs below 256 bytes, at most 128 nodes and depth 32. Raw facts have
one scope, at most one symbol, three definitions, two variants and three prior
names; pools hold 64 type rows and 128 exact IDs. Per-member outputs have at most
four combined diagnostics, one use and one constructor publication. Fixed
annotation shapes are primitive/named types, a single modifier or a two-member
structural union; bounded table scans and these finite shapes bound the work.

Coverage includes original selection guards, ordered diagnostics, full reader
results, a caller-supplied refreshed-fact boundary, publication continuation and
owned results after producer retirement. Defaults are neither analyzed nor run;
layout, final publication and the production record analyzer remain unchanged.
There are no runtime files, workload children, services, network, sleeps, engine
frames or GPU work. This route is outside all aggregates. Independent review of
the exact combined source and workload is required before execution on a new tree under
one unchanged production 60-second deadline, including preparation and cleanup.
Build/configuration timing is separate. Registration is not runtime evidence or
a compiler-bootstrap claim. The landed-tree measurement is recorded below.

The corrected semantic source `b57ac78c53c170139d8f807158c076f971a2202b` passed
all five complete union/effects/records/unit/type selectors in hosted
[run 39140946564](https://github.com/waldnercharles/chibi-studio/actions/runs/39140946564).
Their case counts were 3/4/5/3/4, and each verified its execution identity, exact
summary and separate unchanged 60-second production supervisor. PR444 merge
`c828177eb9d5cd8df52b5ec8f73516a9ea4cb5fb` has the same tested tree
`09d520fe2a5723226126b0879faaee390e87b7b9`. The transport integration combines
that landed dependency with PR445 and remains unmeasured: after independent exact
review, rebuild the ordinary compiler-only language/tools/host products, pass the
normal private link/ownership/symbol audit, then run both full transport selectors
and all five full semantic selectors once each, under seven separate unchanged
production 60-second deadlines. Configuration/build/audit timing is separate.
Do not aggregate, split, retry, import the native-unit candidate or substitute the
prior semantic pass for new integrated-tree evidence.

`python3 tools/test_policy.py tool language-unit-analysis-chibi` selects three
C/pico_unit cases for unit analysis. Twenty-five fixed raw typed-IR/catalog
requests and four actual analyzed sources make 58 authored call attempts across
bytecode and SLJIT, with 500,000 instructions per call. Five compiler sources
below 48 KiB per backend plus four fact sources below 256 bytes total fourteen
preparation/analysis parses. The raw maximum is sixteen expressions, except one
fixed 81-expression forward cast chain; actual source has at most 64 expressions.
Other maxima are 32 symbols, four functions, 64 statements, eight bindings, three
capabilities, two catalog types, two fields, six ports, four arguments, two
globals, 128 IDs, 256 ranges and 32 symbol indices. Outputs contain at most
81 cache rows, 512 terms and 32 diagnostics. Detached results and retained
outputs are checked after producer and module retirement. There are no workload
files, child processes, host services, engine frames or GPU operations.

The focused `language-unit-analysis-zero-id` and `language-unit-analysis-deep`
commands each select one existing raw request in two independent pico cases,
one per backend. Each command makes two call attempts and ten preparation parses
within one unchanged production 60-second deadline. A failure in the first
backend does not suppress the second. These are negative-baseline witnesses;
they do not replace the complete differential corpus or evade its deadline.
C witnesses are asserted before the authored comparison. The zero-ID witness
requires the exact argument-unit diagnostic and known `s` result. The deep
witness requires 81 known `u` values and no diagnostics. Failure-only fixture
logging distinguishes runtime depth exhaustion from budget, preparation and
other failures. A nonzero command exit alone is not the intended red proof.

The original baseline wired the extracted C oracle and authored draft into the
language driver, retaining the zero-ID guard and recursive evaluator until both
red witnesses executed. The merged repair and later full semantic regression pass
are recorded in PLAN.md. The production analyzer remains unchanged. Independently
review every new combined source/workload before execution; configuration/build
timing is separate. All three routes stay standalone and outside the aggregate.
Prior measurements and source review do not validate a new integrated binary or
establish a complete compiler bootstrap.

The compiler port also adds separately reviewed language regressions to existing
routes. Syntax adds two fixed parses for typed declarations, labels and system
state; program adds two preparations and four calls with 1,000-instruction
budgets, checking both backends. Semantic adds nine short sources covering
owned writes and rejected external writes. `language-record-runtime` adds ten
small preparations and fourteen calls with 10,000-instruction budgets. Two-row
records verify that by-value mutation preserves caller snapshots, explicit
references update caller roots, and returned snapshots survive module teardown.
Scalar arguments, reference exposure, inline and standalone entries share these
checks. That standalone route now has five cases and 28 calls total. These additions
create no frames, workers, files, processes or GPU work.

Scalar folding exposed an analyzer bug where unchanged union-element arrays
were treated as single union values and unwrapped. Its `language-program`
regression adds two preparation parses and four event dispatches across the two
backends, each capped at 10,000 instructions. Arrays contain at most two tagged
scalar values. Local append, reassignment and element writes preserve caller
snapshots; a returned array retained in behavior state survives the first
activation. The existing array capability pack supplies the append operation.

## Compiler bootstrap checks

`python3 tools/test_policy.py tool compiler-bootstrap` selects seven cases. They
check that the compiler host's `--bootstrap` mode refuses bad input before any
compile runs:
- mixed or missing options;
- a missing or invalid `bootstrap.kdl`;
- another manifest format, or a version older than the minimum;
- a folder with no manifest, or a closure its manifest doesn't record;
- a missing source, or a package that gained a file;
- a closure recorded for another target;
- a seed folder without a seed, or a seed whose sources aren't on disk.

The SHA-256 check is pinned by four digests: FIPS 180-2's for "abc", and those
of 55, 56 and 64 a's, which sit at the padding boundaries. The cases run about
twenty host children on tiny folders. The target case loads the closure that
`test_language --compiler-artifact-write` writes.

`python3 tools/test_policy.py tool bootstrap-recovery` (`make test-recovery`)
selects one case. It exports HEAD's `tools/compiler` and `packages/core` with
`git archive` and has the unsanitized Release host recover the compiler from
the checked-in `tools/compiler/bootstrap` within 50 s. Set `CHIBI_RELEASE_COMPILER_HOST` or
`CHIBI_BOOTSTRAP_ARTIFACT` to use another host or artifact.

## Game equivalence

`python3 tools/test_policy.py tool game-equivalence` is the gate for PLAN.md
M5 step 4. It rebuilds the stories in `test/stories` with a reference and a
candidate compiler, and compares what the two artifacts of each story declare
and how they play. It is a standalone selector, not part of any group, and
takes about 6 s.

- **Stories.** Each story is a small project that shows one feature, played
  by its `content/scenarios/Story.kdl`. `test/stories/README.md` lists them in
  the order stage one reaches them. Each compiles alone, so stage one can
  pass one story before it compiles another.
- **Compilers.** A compiler is any program that takes `--project-root ROOT
  --output FILE`, as `chibi-compile` does. `CHIBI_REFERENCE_COMPILER` and
  `CHIBI_CANDIDATE_COMPILER` select them. Both default to
  `build/sdk/chibi-compile` until stage one compiles stories.
- **Orchestration.** `test/tools/GameEquivalence.chibi` runs in
  `chibi-build-host`. Its `Rebuild` entry compiles each story with each
  compiler and plays them, and `Diff` compares two traces.
- **Observer.** `test_tools --game-observe TRACE ARTIFACT SCENARIO...` reads
  each artifact on the CPU and replays its scenario from the scenario's
  scene. For each story, the trace holds:
  - a `play NAME` line, NAME being the artifact's file name;
  - what the artifact declares apart from its code: a digest of every byte
    outside its module records, located by `player_prepared_modules`, then
    each module's name and ID (`--native` checks that function);
  - one line per fixed step, from frame 0 (after Begin) to the scenario's
    last frame: hashes of the ECS world, saved state, materials, physics
    bodies, contacts and events, and UI text. Frame N is the world after N
    steps, so input keyed at step K first shows in frame K + 1.
- **Cases.**
  - Both compilers rebuild every story to the same trace.
  - A variant of Motion whose drift is doubled is named in Motion at frame 1,
    with both lines, and its declarations still match.
  - Differing declarations are named. A missing trace, or a story with no
    frames, is refused rather than matched; the observer removes the trace
    unless every story played.

## Compiler artifact runtime checks

`python3 tools/test_policy.py tool compiler-artifact` selects fifteen tooling
cases and two cases in one child producer. Build `test_language`, `test_tools`
and `chibi-compiler-host` separately. The original producer prepares the actual
eight-package source-snapshot compiler closure on both backends (16 source
preparations), serializes its executable modules and checks that reversed record
order yields identical container bytes. It writes five requests and expected
results. A separate producer case prepares one <=4 KiB `binary_fixture` package
on both backends, with exactly four authored exports: `Echo(byte[]) -> byte[]`,
`Scalar() -> byte`, `Integers() -> int[]` and `Loop(bool) -> byte[]`. Stage zero
serializes this <=32 KiB closure and writes its IDs and bounded inputs. It makes
no source calls. This is transport fixture generation, not authored serialization.

Two fresh original product processes load only the snapshot artifact and run
five requests on bytecode and SLJIT. Actual entry counters must confirm the
requested backend without inspection fallback. Requests include a three-file
import graph, syntax failure, invalid UTF-8, embedded-NUL source length and an
exact uint64 maximum declared size. Five source calls plus ten product calls
each permit three million instructions. Four C oracle parses and twelve raw
bridge parses are unchanged: with 18 preparation parses, the route has at most
34 parser calls. Each original request contains at most three input files.
The original artifact cap remains 16 MiB, request JSON 8 KiB, expected/captured
output 256 KiB and stderr 4 KiB.

The raw-facts producer also checks all retained syntax roots and named/extra
flags directly against its four C trees before projecting the owned expected
JSON. One 13-byte comment is added to the existing valid source so serialized
results must contain both named values and both extra values; this adds no
requests or parses. A 1,024-entry C raw-node scratch array and four bounded tree
walks (depth 128) supply the comparison. Two Boolean JSON fields add at most
34 bytes per node, plus the syntax-root integer per document. The existing
producer/product assertions enforce the unchanged 256 KiB output and 16 MiB
artifact caps. The eight-module closure and runtime host limits are unchanged.

One additional native catalog reproduces the preceding thirteen-result parser
signature. One bounded bytecode decode of the actual regenerated closure must
reject that incompatible ABI, leave the aggregate decode budget it was given
unchanged and publish no collection or root. The current descriptor has fifteen
results, with the last two checked as named Boolean arrays and a distinct ABI. There is no
callback invocation, source parse, child or compatibility alias in this check.
The same ABI equality gate also rejects an old-signature artifact against the
current host; this fixture directly exercises only the current artifact against
the previous host descriptor. The language producer adds the runtime native-prepared helper dependency for
this check; the product host still has no semantic analyzer/lowerer dependency.
Existing on-runner cap assertions are the resource checks; exact size export is
not a separate gate.

The original three direct malformed-container reads reject truncation, duplicate
IDs and trailing data before executable decoding. The original bad-magic CLI
child remains. Two fixed C-built provenance artifacts (<=64 KiB each) declare a
reference argument or a zero-initialized reference result using `BF_POINTER_TYPE`.
The result fixture has no arguments or operand slots and only `BF_END`. The
fixtures are prepared/loaded but never execute: numeric argument input and
malformed result-fixture input must fail provenance admission. No raw-pointer
execution or additional source parse is introduced.

Nine added tooling cases cover these finite binary operations:

- Four direct successes return all 256 octets and a typed empty array on both
  backends. Input, prepared bytes and loaded host retire before retained bytes
  are compared. Exact selected-backend entry counts are checked without fallback.
- Two direct calls return 256 bytes against a 255-byte limit and fail atomically;
  the 256-byte successes above cover the exact boundary. Two invalid API limits
  (zero and `COMPILER_HOST_MAX_OUTPUT_BYTES + 1`) reject before execution.
- Two wrong return signatures reject before malformed JSON parsing; malformed
  JSON and wrong argument count add two more pre-execution rejections.
- The reference-argument fixture adds one bytes-API rejection. The new result
  fixture adds one JSON and one bytes-API rejection, with zero entry counts.
- Two direct `Loop(true)` calls fail with a 100-instruction budget and no bytes.
- Two fresh binary-output children echo the valid small closure on bytecode and
  SLJIT, replace a preseeded pathname containing spaces, leave stdout empty,
  report exact backend counts and leave no staging residue. Each output reloads
  on its matching backend and makes one direct two-octet echo call after the
  child and loaded-artifact input buffer have retired.
- Three bytecode children echo nonartifact all-octet bytes, empty bytes and a
  wrong-target prepared closure. They fail before publication and preserve a
  preseeded sentinel. The last case changes only the little-endian pointer-width
  u64 at prepared-payload offset 16; the CHIBICLO header, row and payload length
  remain valid. It requires `prepared target ABI differs`, proving full runtime
  validation rather than outer-header checking. This replaces the proposed new
  truncated-output child; truncation remains the original direct decoder case.
- One child targets a nonempty directory. Successful invocation/validation is
  followed by rename failure; the directory, sentinel and staging cleanup are
  checked. One already-expired child deadline preserves its destination. Its
  separate `envp` overrides only `CHIBI_TEST_DEADLINE`; the parent environment and
  supervisor deadline are unchanged. Two final usage children reject duplicate
  output switches and batch plus binary output without execution.

Exact workload: 15 tooling cases, two producer cases, 13 supervised children
(original producer, two JSON products and bad input; two publications, three
bad outputs, rename failure, expired deadline and two usage checks). There are
20 direct host-API attempts: the original reference-argument rejection plus ten
new executions and nine new pre-execution rejections. The three direct malformed
container reads and the one additional raw-syntax ABI decode are separate; the
ABI check adds no source preparation, callback invocation or child process. There are 31 top-level calls that execute: five
original source, ten JSON product, ten new direct and six new binary product
calls. Backend entry counters can include nested calls; this total is not their
sum. New echo calls permit <=10,000 instructions; the two loop calls permit 100.
Binary input JSON is <=256 KiB, artifact <=32 KiB, stderr <=4 KiB and successful
binary stdout empty. Each child retains its <=10-second cap inside the one
unchanged 60-second production deadline. No extra producer process, standalone
probe, network, renderer or GPU work is admitted. The disposable directory and
all fixture outputs are removed. This route remains outside the tooling aggregate.

Extraction copies each `pin_array.items[i].u8`, not packed `pin_value` storage.
The all-octet/boundary/empty cases cover this public path. The exact declaration,
actual return header, element type, metadata, item types and count/capacity
arithmetic are independent source-review gates. Verified authored code and
admitted array primitives construct typed, owned arrays; the fixture cannot
produce a forged header, negative count, missing nonempty item storage or
wrong-typed item. Such corruption cannot be safely introduced by executing
fabricated native pointers. The fixture's echo capacity equals its bounded input
count, so its 64 MiB representation ceiling cannot be reached; the division,
header-plus-capacity and packed-length guards require static arithmetic review,
not a huge allocation probe. No whole-arena runtime memory bound is claimed.

Binary SIGPIPE handling, checked precommit counter write/flush and absence of
post-rename I/O/status checks are source-review gates. This finite matrix does
not claim closed-stderr, active-signal, partial-write, close-failure or crash-
durability process coverage. Prepared/native capability compatibility still
belongs to the existing runtime decoder; this slice adds no compatibility
implementation or claim of exhaustive emitted-native-ABI mutation coverage.

The private JSON input transport preserves invalid UTF-8 bytes and uint64 values;
ordinary JSON adapters are unchanged. Product processes perform no compiler-source
preparation, native semantic analysis or lowering. Echoing stage-zero prepared
bytes does not prove a complete authored compiler, authored serializer,
stage-one/stage-two bootstrap, cross-architecture portability or source-free SDK
installation.

Module-set validation measures generations with `execution_prepared_measure`
instead of serializing them, so the runtime read path never references the
public `execution_prepared_write` entry point. The Linux GNU/Clang production
`chibi_execution` target's private function and data sections and Darwin's
dead-strip link discard it from the host. This is an executable-link contract,
not removal of every native encoder: the prepared reader legitimately uses
shared canonical encoding for dependency fingerprints. The static archive still
exports the writer, and direct producer/tooling fixture calls must retain it.
Each platform needs actual linked-binary evidence; unsupported platforms gain
no exclusion claim.

Inspect the exact unstripped product's actual link inputs and defined symbols.
Require the loader, JSON/bytes/batch invokers, artifact reader and prepared reader;
exclude the public writer, source preparation, native authoring, semantic
analysis and lowering. Positively require `execution_prepared_write` in both
`test_language` and `test_tools` and execute their real fixtures. Use the normal
production target flags; validation-only section flags, stripping or hidden
symbols do not establish the boundary. Configuration/build timing and link
inspection are separate from the unchanged supervised runtime deadline.

The existing two-case `package-ownership` route also verifies both private host
executables and their directly linked implementation objects under Ninja and
Makefiles. The compiler host adds four tiny C compilations, two executable links
and one wrong-owner inspection. The route has 22 supervised child invocations
(six configure/build/install commands and sixteen inspections), each bounded to
ten seconds within the shared 60-second ceiling. It compiles thirteen tiny C
units and one precompiled header; none of those fixture executables runs. Missing
or changed installed compiler-host bytes and linking its implementation only
into an unrelated executable must fail provenance checks.

## Private build-host process checks

`python3 tools/test_policy.py tool build-process` selects eighteen C cases in the
existing tooling dispatcher. Build `chibi-build-host`, `test_tools` and its
`policy_worker` dependency separately. The route uses one disposable directory,
fixed authored sources, at most 54 controlled process launches or failed
spawn attempts, and one shared 60-second production deadline. It performs no
engine configure/build, network, renderer or GPU work. Case deadlines are at
most two seconds, with shorter deliberate timeout/cancellation budgets; every
native case clamps its budget to the inherited deadline.

Cases cover exact argv and cwd, explicit environment and parent isolation,
separate output bounds, exits/signals, spawn errors, timeout, active cancellation,
and descendants whose leader exits first. The private host executes both the
controlled source and unchanged recording workflow on bytecode and SLJIT, with
actual backend counters and failure-path assertions. Compiler-enabled full
native builds also verify absence from the ordinary game catalog; minimal
compiler-only builds verify rejection by a catalog without build capabilities.
Two CLI invocations cover source loading, a nonzero authored result and a missing
entry. Seven additional cases cover argument and returned-value lifetimes,
string operations, truncated or binary output rejection, regular-file and size
limits, and atomic write failure cleanup. Their fixed file workload is about
7 MiB. The original eight `language-build-workflow` recording cases remain an
independent admitted route.

`python3 tools/test_policy.py tool build-toolchain` selects seven C cases for the
installed authored build program. Five cases make 50 API invocations across
bytecode and SLJIT; one runs the host CLI with exact tail arguments. Twelve tiny
Git roots and at most 57 controlled CMake attempts cover compiler/environment/
cache precedence, explicit typed and split definitions, loaded inputs, compiler
identity, private dependency overrides and their precedence over the cache,
failure/retry and exact argument forwarding. The seventh case uses the
production Makefile to select recovery for missing/old hosts and authored
orchestration for a compatible host, including paths with spaces; three outer
and three recursive Make calls run marker recipes only. The process route also tests the private protocol query used by this
transition. Python runs only the production input probes; no Python tests are
introduced. These cases never configure or build the engine. Actual clean-source
configuration and compilation are separate build evidence.

`chibi-cli-installed` runs the packaged SDK's launcher in place, `dist/sdk/bin/chibi` unless
`CHIBI_INSTALLED_CLI` names another, so run `make package-sdk` first. Its three cases cover
`version` and `doctor`, then `check`, `fmt`, `build` and a native `export` of one tiny project,
`test` on one package tree that imports the installed `core:` collection, and `doc` on one
two-page site with the installed generator. It takes about three seconds and starts no
player.

`build-process`, `build-toolchain`, M0's `chibi-cli` and `chibi-cli-installed` remain focused routes. The complete
`make test-tools` group builds a runtime-only tooling driver and does not build
the private host or public launcher. Route registration does not add either
fixture to that aggregate. Current platform results belong in `PLAN.md`;
Windows does not build or select the private host.

## Batch validation (CUT62)

Publication uses independently reviewed validation for the affected contracts.
A change spanning several subsystems can use focused checks when review accounts
for each changed contract and their interactions. A mixed scope label or an
unavailable full suite does not automatically block landing.

1. **Review and choose checks.** Independently inspect the diff, callers, tests
   and dependency/build effects. Record which contracts changed and why the
   selected admitted checks cover them. Include applicable backend, sanitizer,
   load, web/UI and release validation. File extensions and selector names alone
   do not establish coverage. Resolve unknown impact before publication. When
   affected coverage is quarantined, provide a reviewed admitted replacement;
   otherwise that specific coverage gap blocks landing. Unaffected quarantined
   suites remain disabled and not passed, without blocking the change.
2. **Freeze and validate.** Use a clean candidate based on freshly fetched main.
   For a batch, pin each reviewed source and review the combined interactions
   and conflict resolutions. Record the candidate commit/tree, configuration,
   commands, exit codes and logs. Run selected checks, formatting and affected
   builds against that candidate. Use `make web` and web verification for UI
   changes. Preserve assertions and failure cases; previews do not replace them.
3. **Publish the tested tree.** Recheck main and sources, explicit PR/ticket
   holds, required approvals and actual server protections. Land the complete
   tested combination atomically through the authorized workflow. A new hold
   blocks publication even when Git is unchanged. Held code stays excluded,
   including indirect dependencies and copied patches. If the base, source,
   tree, conflict resolution, dependency or validation flags change, review and
   validate the new candidate. Intermediate batch trees require their own review
   and validation.

Local execution evidence is sufficient when CI is disabled or unavailable unless
server protections actually require CI checks. Workflow configuration describes
possible jobs, not mandatory execution of every matrix row. Inspect applicable
server requirements before landing; required checks and approvals remain binding.
An absent CI job is not a pass. Preserve existing backend and configuration
requirements for each selected check, and report other coverage as unexecuted.
Full native/render suites remain quarantined under the 60-second policy.

A server-created merge/squash commit may have a different ID. Prove identical
tested tree and unchanged base and source revisions, and satisfy current-head
server requirements. Tree equality does not transfer a required status check or
replace a metadata-dependent check. Plans and component-head results grant no
merge authority.

### Planning helper

```sh
python3 tools/agent.py validation-plan --base origin/main --scope mixed --review-note 'Independent review: affected tooling and runtime contracts covered by selected checks' --tools --selector=--source-edit
```

The helper records Git identity and admitted commands; it does not execute them
or certify coverage. Both `focused` and `mixed` require a review note and selected
checks. `unknown` records unresolved scope. Use `--tools` (or
`--tool=native-tools`) for the C tooling group and repeat `--selector=--name` for
admitted CPU selectors. For batches, repeat `--source` and confirm eligibility
only after checking every source's review, holds and approvals.
`--verify <saved-plan>` checks local identity, not remote freshness or execution.
Keep saved plans outside the worktree.

## Historical timing evidence

The original exact native admissions are recorded in
[test quarantine](test-quarantine.md#retained-short-coverage). The former tooling
suite's 24.4-second parallel run and longer native/Studio/render aggregates
belong to their recorded revisions and removed runners. They are not measurements
of the new C group. PR186 retains the original process-cleanup reproduction and
startup-margin evidence; current results must identify the actual reviewed head.

The independently reviewed `text-format` tool selector runs two fixed public native
formatting cases, with 41 assertions covering scalar formats, owned string and
array display, rejected printf modifiers and bounded placeholder indices. It
passes in 0.799 seconds under ASan/UBSan; there is no world, process or GPU work.

`native-values` selects one independently reviewed native SDK case. It checks
canonical ABI lookup, NaN comparisons, integer saturation, nominal enums, vector
conversion and output ownership after input/table retirement. The scoped native
consumer passes 43 assertions in 0.403 seconds, without files, processes or GPU.
`box3d` selects one in-tree check of the `chibi_box3d` package (Box3D v0.1.0,
the copy Cute pins). A box falls onto static ground and comes to rest, then
closest raycasts hit the box, hit the ground and miss. It needs a native tools
build and passes 14 assertions in 0.052 seconds, without files, processes or GPU.
`interior-border` draws a circle and a diamond with Cute's interior border into
a hidden 64 x 64 canvas, opaque and translucent, through Cute's instanced and
tiled draw paths, and compares every pixel with an analytic reference. It is a
GPU check that needs the render-enabled tools of the main build: run
`make build BUILD_TARGET=test_tools`, then
`make agent-gpu-test AGENT_SELECTOR=--interior-border`, which runs that build's
`test/tools/test_tools` after the agent's device preflight. Pass the same
`BUILD_DIR` to both to check another build. On lavapipe it passes
9 assertions in 6.1 seconds on a cold shader cache and 0.9 seconds warm. The
tiled case fails when Cute's compute renderer is unavailable. Every SDL_GPU
backend has one, so there a failure means Cute's tile shaders did not load.
`viewport-resize` creates an engine viewport at a 640 x 360 placeholder in a
hidden app. The first requested size must replace the canvas on that draw, and
a later size only once it has been requested for two more draws. A press above
a level perspective horizon stays captured without a ground point; below it,
the pointer lands on the ground. Build and run it like `interior-border` with
`AGENT_SELECTOR=--viewport-resize`.
Four additional cases check default and authored lens settings across four
aspect requests, the finite projection of a field of view just below pi, and the
pointer ray against one displayed 64 x 64 image. Camera projection and ray
inversion must agree, authored lens values remain unchanged, and out-of-image
or non-perspective pointer requests must fail. A requested camera or resize keeps
the pointer ray aligned with the last presented camera until the next draw.
These checks stay in the same hidden app and shared deadline.
Its UI case draws one 64 x 64 frame containing a translucent red rectangle
and a red glyph over opaque blue. One readback compares direct presentation with
the public straight-alpha texture copied into the other half of the target.
It requires correct source-over color, partial glyph coverage, and straight RGB
in the exported texture. The earlier four-case workload passed in 1.219 seconds under the shared 60-second
deadline on macOS with the native Metal renderer. That measurement does not cover
the current nine-case selector. The combined nine-case run passed in 2.448
seconds of execution, or 2.668 seconds including the production supervisor. The old presentation path
failed the new case with red 255 where source-over required 128. Vulkan discovery
was unavailable on this host and does not describe the Metal execution. Use `make agent-gpu-test BUILD_DIR=<render-build>
AGENT_SELECTOR=--viewport-resize` after building that directory's `test_tools`.

`scene2d` draws 2D scenes through the scene renderer and an engine viewport in a
hidden 64 x 64 app and reads back the app canvas. A scene without `HRC2D`
must show its materials unlit, `hrc2d_defaults()` lighting must keep them
visible, and the unlit view or removing the settings must stop lighting. A
sprite whose normal map faces a light must be lit more than flat normals, and
mirrored, less, unless a Material turns its edge light off; a map must be loaded
and the size of its image, and relief finite. A lamp drawn as an Appearance must
light a closed floor beside it in Depth thickness by its relief, scaled with its
visual, and none of it from the plane, and a relief that isn't finite
must draw nothing. Build and run it like
`interior-border` with `AGENT_SELECTOR=--scene2d`.
`radiance` runs HRC in a hidden 128 x 128 app and reads it back: an emitter
lighting open space through its material, opaque, finite, bouncing and nearly
transparent walls, reset, empty and resized grids, reused capacity against a
fresh grid, color two and four times coarser than brightness, work cells of
fractional pixels, a spark and a thin wall through split color and a
half-resolution gather, lighting that normalization fitted, every resolution,
color and gather combination, the adaptive ladder, GPU pass timing, and the
extend and filter passes alone against CPU references. The cases pin the
lighting they were calibrated with (finest grid, no ambient, intensity 7.5)
rather than the authoring defaults. On lavapipe it passes 645 assertions in
24.3 seconds, 30.5 seconds with an empty shader cache. HRC's view layout and
lighting normalization run without a GPU in `render-resources`, and so do
material borders: rectangles that cover exactly the cells near empty space,
their build scratch, and their cache's hits, replacement, eviction, limits and
a crowd of 20,000 masks.
`radiance-depth` lights Depth thickness from material inputs written by hand,
in a hidden 32 x 32 app with 128 x 128 targets for light that travels farther,
from the same pinned lighting but with color at half resolution: slabs that
shadow only the light they cross, by height whether or not they absorb, unless
they glow, small, moving, edge and mixed-color sources, falloff to the depth
radius, perspective views, the depth gain on receivers that absorb, as much at
a Material's full absorption as opaque, receivers that absorb taking the light
HRC brings open ones at falloff 0, 1 and 2 within a 15% tone-mapped brightness
tolerance, independently of intensity, less from lights near their plane and
none from below it, receivers that absorb nothing taking
HRC's light by facing and height, smoothly, and by each light as much as HRC
brings it, so a light behind a wall that absorbs leaves them as lit, also when
it is the only light in reach, and one behind a faint absorber fades smoothly
with its absorption, walls hiding
ambient light by their height in both visibility modes, soft visibility that
keeps a light on an open floor's plane and past a slab's top or bottom as lit
as hard visibility does, gloss, every depth quality, depth history of light
and of that shading, segments and steep planes. A bright grazing source with
strong relief keeps the same displayed light when its power increases a hundred
times and the receiver albedo decreases by the same factor, in both visibility
modes and through fresh and retained Depth history. The surface gather pass alone
must take each quadrant's light, find crests past gaps and stay dark without
light in reach, and surface gather light must restart when the view turns.
Its cases share one HRC, since a new one compiles all of HRC's shaders. The
reviewed 34-case workload also checks that changing falloff drops stale Depth
history. The bright-source case fails without the shared-exponent storage for
the brought light and signed shading sums. All 34 cases pass in 6.4 seconds
on the native macOS backend. The earlier 33 cases passed 2427 assertions in
10.7 seconds on lavapipe, 20.0 with an empty shader cache.
`material-sampling` draws shapes into the material raster alone in a hidden
65 x 49 app: small circles must emit their area wherever they land on the pixel
grid and at twice the resolution, overlapping tiles must keep their draw order,
a moving ring of boxes in one tile group must keep its emission, and every
merged border must write the five targets its cells do. It passes 1330
assertions in 2.4 seconds, 3.2 with an empty shader cache.
`radiance-surface` lights solids through relief materials in its own hidden
128 x 128 app and from the pinned lighting of `radiance`:
edge light only on a silhouette's lit side, relief facing the light and below
a ceiling, a surface radius in world units, surface colors that keep the
direction they came from in every shadow mode, surface light history, and sky
light that reaches open ground whole whatever the falloff, never glows in open
air, reaches the ground under an overhang only from beside it and leaves a
closed room dark, on the full grid, a coarser one and with split color. It
passes 177 assertions in 29.1 seconds, 39.3 with an empty shader cache.
`radiance-horizons4` and `radiance-horizons8` check relief shadows with four
and eight horizon directions: horizons that follow moving relief without a
reset, the horizon hierarchy against the fine scan, and the horizon cache
against fresh horizons through 15 stages of light, geometry, camera, reset,
resize, mode and fusion changes for all 8 sets of fusion, reprojection and
hierarchy. Each passes 2866 assertions in 23.3 seconds, 30.7 with an empty
shader cache. They run apart because lavapipe maps GPU memory a page at a
time: the cache case peaks between 46000 and 56000 of Linux's 65530 mappings
per process.
`material-pixels` checks what the material raster draws for HRC in a hidden
128 x 128 app: Set Material Parameter overriding one entity until its world is
cleared, a world-space material following its layout frame, a graph material's
parameter through the raster and its own fragment program inside a shape graph's
circle, mixed batches of graph boxes, plain boxes and meshes uploading only what
they draw, a sprite's normal and height maps tilting and raising each texel,
mirrored and turned with it, sprite height maps scaling relief before a
material's Height adds to it, and shapes standing at their relief, raised
further by a material's Height, and fluids landing where game Y points down, gas
joining and parting, crisp unlit liquid edges, burning cores, plumes without
holes, fading parcels and wisps along their velocity. Gas and liquid must take
HRC's light and the wall's shadow with one and three traced cascades, a
refracting liquid must bend only the background behind it, and a crystal must
mirror the scene beside it. All cases share the production 60-second deadline.
These selectors need the material graphs of the main build.
`sdf-visibility` renders 14 small scenes of meshes and SDFs hiding each other
(occluders, cutouts, holes, partial cover, draw order, equal depth, perspective,
near clipping, a mixed 2D layer, and a later frame without the occluder) with
hidden-surface rejection on and off, and requires the same pixels both ways and
the expected depth snapshot copies. An indexed mesh asset of the occluding box,
split into three material groups, must draw the box's pixels and bound and
pick like it. It also checks Cute's whole-canvas depth copy
on its own: a cropped draw after the copy must not reach the snapshot. It builds
shader graphs, so it needs a compiler build, and it reads the shaders from
`src/render/shaders`. Build and run it like `interior-border`
with `AGENT_SELECTOR=--sdf-visibility`. On lavapipe it passes in 8.9 seconds
with Mesa's shader cache disabled and 2.9 seconds warm.
`sky-files` selects four in-tree Radiance `.hdr` cases: flat and run-length
decoding, fourteen malformed files, preparation from mounted content (read
once, with an identity that follows the texels) and import that never replaces
a file. It needs a compiler build, uses one temporary folder and passes 661
assertions in 0.12 seconds under ASan/UBSan with leak checks, without
processes or GPU.
`mesh-files` selects eight in-tree indexed `.mesh` cases: tangents that follow
texture directions (mirrored, rotated, tilted normals, unvarying coordinates
and a sliver beside a large triangle), preparation from mounted content (read
once, with part coordinates stored only when present), ten refused meshes,
each with its reason (bad indices, degenerate triangles, bad tangents, and
groups that split triangles or skip indices), unreadable files (retired
`.mesh.json` paths, text, truncated bytes and an older version), the largest
mesh a bake can write with one more triangle refused, a model export whose
`model.mesh`, OBJ faces and provenance agree, pick trees freed by each
publish, clear and exchange of the mesh catalog, and a pick of a quad 2e20
across. It needs a compiler build,
uses one temporary folder and passes 9,929 assertions in 9.2 seconds under
ASan/UBSan with leak checks, most of them writing and reading the largest
mesh, without processes or GPU.
`model-import` selects seventeen in-tree glTF import cases: the image, Standard
materials, mesh and prototype a `.gltf` writes, each once, byte for byte the
same from a `.glb`, and nothing for a material only a hidden node uses; names
made unique, ignoring case, and cut to 63 bytes; material names for a model
named as long as a name may be; an import that would replace a file, or fails
partway, leaves the content as it was; material and prototype names another
file in the project already declares, declaration folders it cannot read
and files it cannot parse, materials past the native graphs a project can
prepare, and content folders and folders in them that are links, which it
refuses;
refused extensions, texture transforms,
second UV sets, factors beyond float range, images and primitives; unreadable
files, names and buffers, buffers that `..` or a link leads outside the model's
folder, and buffers that together pass the size limit; unlit materials that
keep only their base color; the warnings for what it leaves out, and the
vertices only a left-out triangle used; a primitive whose accessors carry
more vertices than a `.mesh` file holds, of which its triangle uses three; a
model whose nodes copy a primitive past what a `.mesh` file holds, refused
while it bakes, and a primitive past it, refused before its vertices are
built; and tangent frames whose OpenGL green tilts up the image: generated,
authored, turned, mirrored by a node, flat at any size, flattened onto the
ground by a parent, scaled past float's volume or square, and carried past
its range. It needs a compiler build, uses one temporary folder and passes
176 assertions under ASan/UBSan with leak checks, without processes or
GPU.
`model-graphs` selects 39 model graph compile cases: primitive solids,
transforms, booleans and smooth unions with their distances and conservative
bounds, functions, radial and linear arrays, runtime parameters prepared
without recompiling, expressions, hit resolvers and their part limits,
profiles extruded, revolved, offset and swept, rims, level sets, Bézier and
pivoted sections, anchors, models that instance other models with overrides,
stable part paths and cycle diagnostics, part inspection, and sampled SDF
atlases that validate, interpolate, extend past their border and bake a mesh,
with save and reload roundtrips. It needs a compiler build and passes 3,419
assertions in 0.4 seconds under ASan/UBSan with leak checks, without files,
world or GPU.
`model-catalog` selects 27 cases of models and materials in the renderer's
shader catalog: runtime parameters whose binding texts preparation must accept
before a snapshot reads them, refusing a value past its range, Set Shape
Parameter overrides and their cache, slot-scoped material parameters and
ambiguous unscoped names, saved model sources that validate against their
materials and shader libraries, material slots that own hits through CSG,
transforms and instances, parameter and ramp limits across slots, catalog
edits, renames and removals, and model surfaces: bounds, transforms, empty
intersections and perspective picking. It needs a compiler build, uses one
temporary folder and passes 1,355 assertions in 0.4 seconds under ASan/UBSan
with leak checks, without a window.
`render-geometry` checks render geometry without a window: appearance shapes'
bounds, containment, points and rotated bounds, refusing geometry that is not
finite and positive, and mesh surfaces' world bounds, banking with a visual
roll, frontmost picks, compound mesh parts that project preparation publishes,
and unit normals on every supported primitive. It passes 817 assertions in
0.05 seconds under ASan/UBSan with leak checks.
`arena` checks the support arena allocator: growth that keeps earlier
allocations intact and aligned, a reset that hands back the retained blocks in
order, a zero-initialized arena, and collectable values found by address
through the page index across sweeps and a reset. It passes 360 assertions in
0.06 seconds.
`shader-graphs` selects 15 shader graph cases on the CPU: planar and
cylindrical mappings, bump and normal-map frames through part transforms and
scaled seams, sampled and triplanar heights, triplanar axes, decal volumes,
nested derivatives the validator refuses, generated GLSL that never names the
reserved `sample`, mapping, Fresnel, remap and Voronoi effects, color ramps
whose stops the node owns through save and load, and image textures by color
space, filter and wrap, with their missing-image diagnostics and authored
settings. It needs a compiler build and passes 166 assertions in 0.08 seconds
under ASan/UBSan with leak checks, without files or a window.
`shader-parity` selects six hidden-app 32x32 cases that run a compute shader of
shader graph GLSL and compare it with the CPU evaluator: decal masks, effects
over seven input sets, ramps and cells, image textures over every color space,
filter and wrap at finite and nonfinite UVs, and a sampled SDF's distances and
normals at 256 points. A sampled SDF image must keep its pixels, texture and
last good shader when a reload's digest differs or an edit is refused. Build
and run it like `interior-border` with `AGENT_SELECTOR=--shader-parity`. It
passes 64 assertions in 2.3 seconds under ASan/UBSan with leak checks on Vulkan
llvmpipe.
`shader-programs` selects eleven hidden-app 64x64 cases. A ramp's numeric edit
reaches fragment, 3D preview and raster programs without a recompile, and a
structural edit recompiles once; a raster keeps its last good frame when a
shader fails to compile or ramps pass the table's capacity; two materials read
their own table offsets; and program sets keep their ids as the table grows.
Image textures route through materials with more images than a program binds,
smooth unions of different materials, fragment programs sampling like the CPU
across seams, shapes cut by an image's alpha, and a reload past the side limit
that keeps the last good image. It reads its images from memory. Build and run
it like `interior-border` with `AGENT_SELECTOR=--shader-programs`. It passes
70,156 assertions in 6 seconds under ASan/UBSan on Vulkan llvmpipe, whose
worker threads leak 112 bytes as they do for `material-ramps`, so it runs with
`detect_leaks=0`.
`material-cameras` selects six hidden-app 96x80 cases of the material
raster's orbit camera. Projected bounds of a box under oblique, rolled and
near-plane cameras must hold every corner an independent clip keeps, and an
eye circling the view's center must keep its basis and radius. A mesh and an
SDF swap which is in front as the camera turns half way around, a 2D layer's
marker lands where a scalar projection puts it, an eye circling a four-actor
cross sees each side in turn, and emissive SDF spheres project their emission
where the orbiting eye sees them until an opaque mesh hides one. It passes
8,045 assertions in 4.8 seconds.
`mesh-materials` selects eight hidden-app 96x80 cases of meshes in the material
raster: a box's visible faces, relief and emission, hidden parts that add
nothing, depth between meshes rather than actor order, ordered 2D layers that
override it, pitched and rolled planes, even subpixel coverage, padded views
across a resize, imported meshes that sample their own UVs and hide their back
faces, and perspective cards of known size, depth, side faces and near-plane
crops. It passes 151 assertions in 4.6 seconds.
`sdf-quality` selects ten hidden-app 96x80 cases of SDF surfaces: programs
specialized for one shape, a growing sphere's analytic heights and normals at
every resolution, pixel error and normal mode, a thin ring's coverage and hole,
planes and creases that keep real faces, a rasterized tilted box top, trace and
normal evaluation counts, analytic gradients under transforms and their
fallback for unsupported and singular graphs, and spatial scratch groups that
draw what one scratch does with less work. It passes 118 assertions in 4.3
seconds.
Build and run these three like `interior-border`, with `AGENT_SELECTOR` set to
the selector. They pass under ASan/UBSan on Vulkan llvmpipe, whose worker threads
leak 112 bytes as they do for `material-ramps`, so they run with
`detect_leaks=0`.
`scene3d-post` selects nine hidden-app cases of 3D post-processing over an
emitter pattern drawn straight into its HDR target: the target keeps scene
light and the Neutral tone map matches a CPU reference, bloom spreads past its
threshold while zero strength, zero radius or a high threshold leave the image
untouched, exposure scales it, infinite channels tone map to white and NaN ones
to black, bloom clamps at the image edge, its radius follows the viewport height
and crosses blur levels smoothly, outputs take sizes down to one pixel,
processing keeps the caller's transform, projection, scissor and viewport, and
invalid settings keep the last output. It passes 59 assertions in 1.5 seconds.
`scene3d-shapes` selects fourteen hidden-app 128x128 cases of the 3D path's
primitive solids lit by the Environment's ambient light. Overhead cameras
center and map the ground, picks find the nearest solid of every primitive,
turned bounds fit, frame and clip at the near plane, and flat triangles turn
about their area center. Every solid primitive draws, 256 small boxes, prisms
or octahedra each become 256 instances in at most four draws, nearer solids win
in any order, parallel geometry draws exactly what serial geometry does for 1,537
actors, fitted bounds frame what draws, prisms and octahedra draw and pick
their outline at eight headings and from below, eight tetrahedron parts
compose an octahedron, polygons' notches and holes stay empty in both the mesh
and picks, and a box frame's twelve part entities match the frame. It passes
1,076 assertions in 2 seconds. Run both like `interior-border`; they also run
with `detect_leaks=0` for llvmpipe's 112 bytes.
`material-ramps` selects four hidden-app 64x64 cases. Nine full color ramps
fill the first 288 entries of a program's ramp table, so a last ramp's stops
lie past the 4 KB of a uniform block SDL's Vulkan backend exposes. That ramp
must show its color through a 2D draw shader and in the material raster's
scene target. The third case only compiles every program kind with the table,
which cannot catch a binding collision, and the fourth counts the ramp table
among the samplers a raster batch binds, which GLES3 limits to 16, among those
of a 3D shadow program, which binds none of the lit pass's maps, and among
those of the lit pass with global illumination's probes and without them.
Build and run it like `interior-border` with `AGENT_SELECTOR=--material-ramps`;
it needs the material graphs of the main build. It passes 43 assertions in 2.2
seconds under ASan/UBSan on Vulkan llvmpipe, whose worker threads leak 112
bytes, so it runs with `detect_leaks=0`.
`graph-model` selects two independently reviewed public consumer cases for nested
value copies, opaque artifact replacement/destruction and topology mutation
rules. They pass 25 assertions in 0.355 seconds under ASan/UBSan. The consumer
links only graph storage and its declared type/execution metadata dependencies.
The prepared-format-v5 state capture passes three compiler writer cases and 161
assertions, seven separate runtime consumer cases and 396 assertions, and one
process supervisor case with five assertions in 0.791 seconds. Its new state case
checks retained banks across publication and restart, plus retrigger deadlines.

The `--fields` selector also checks native field artifact publication with one
pure scalar library, one dependent field and three single-cell evaluations.
A valid library edit publishes a new field revision and value; an unsupported
operation refuses the replacement and preserves the previous revision and value.
This replaces the obsolete test that registered an executable library graph in
the preview and edited a `.chibi` presentation as executable state. Preview
executable libraries now require canonical source publication. Three more cases
lower `FieldShapes.chibi` into a graph library and link `MaterialIslands.kdl`
against it, retain a radius parameter default of 40 in a call that samples -40
at its center, and publish library edits to a live preview. The field relinks
while its valid or invalid unsaved draft stays intact. A library that drops the
called function is refused while the field keeps its revision, value and draft. `--validate-project` checks that
`validate_project` and `preview_field` link a field against a `.chibi` library.


## Terrain Lab save and restart

`python3 tools/test_policy.py tool terrain-playable`, also part of `make
test-projects`, drives the authored controls of `test/fixtures/terrain-lab` on
the VM and SLJIT. For each backend a writer process digs across a 128-cell chunk
seam and saves; a fresh reader process checks pristine terrain, then loads and
checks the restored cells, their collision holes and an unchanged reference
patch. No GPU is used. `--terrain-playable-render` runs an installed player on an
artifact the installed compiler prepared from the same fixture
(`CHIBI_TERRAIN_ARTIFACT` and `CHIBI_TERRAIN_RESOURCES`) for four hidden 640x360
frames and checks the saved PNG.

The CPU `scene3d-terrain` route also checks an empty volume before and after
cache reuse and an origin change. One constant field bakes a 3x3x3 lattice;
three surface queries must retain zero drawn meshes, caster meshes and boxes,
while the rebuild counter distinguishes reuse from reconstruction. This adds
one case and seven assertions to the existing six-case route, with no frames,
rendering, files or child processes. ASan/UBSan validation keeps the zero-length
mesh-copy regression observable under the same shared 60-second supervisor.

### Configurable source layout and whitespace maps

The existing C `--format` dispatcher adds nine finite cases for SDK defaults,
1/2/16-space and tab indentation, width boundaries (including UTF-8), nested
lists/records/parameters/calls/binary expressions, comments and literal bytes,
edit-map reconstruction and caret affinity, invalid options/drafts, and
composition with the comment-preserving semantic minimizer. Fixtures are tiny
in-memory strings; no worlds, clocks, files, processes, network or GPU are added.
The map checks walk at most the bytes of those fixed strings.

The existing bounded native corpus (at most 256 files/8 MiB) now verifies three
fixed configurations: defaults, two spaces at width 80, and tabs at width 40.
Each configuration reparses the result and checks idempotence and immutable
input. `language-format` owns that corpus; `wasm-format` runs the same finite C
cases through Node without a browser or corpus filesystem dependency. Both use
the unchanged production 60-second supervisor. Configuration/build timing is
separate. This is SDK/printer evidence, not controller, Studio, RML/RCSS,
rendering or browser integration coverage.

The first successful measured expanded runs passed 38 native cases/2,944
assertions (89 corpus files, 63,497 source bytes, all three modes) in 8.606 s,
and 37 Node/Wasm cases/2,583 assertions in 7.816 s. Native used Clang 19
ASan/UBSan (leak detection disabled); Wasm used Emscripten 6.0.10 and Node 24.
The initial expanded run exposed a test-oracle mistake: empty documents are
syntax errors in the current grammar. The corrected case preserves that
rejection and tests insertion affinity on a valid empty behavior instead.

### Package-source separator compatibility

The existing C `--format` dispatcher adds one case with sixteen fixed
input/output pairs plus five preferred-width variants. It covers CR, LF, CRLF,
mixed and blank lines, explicit semicolons, alias/blank imports, header-only
input, inline/standalone/multiline block comments, line comments, contextual
legacy names with internal CR blank lines, default/reflow indentation of a nested
array, and package CR/LF/CRLF comment-column tracking with a legacy width
control. CR remains
part of a `//` comment until LF, matching the existing grammar. Comment and
other token bytes remain exact; package whitespace gaps canonicalize to LF.

The successful fixed path makes 42 formatter calls and 84 canonical parses
across twenty-one configured formatting pairs. Each
input and expected output is at most 58 bytes. Existing helpers check exact
output, immutable input, idempotence and whitespace edit-map reconstruction.
No source generation, files, processes, authored/backend execution, worlds,
frames, network or GPU are added. The redesigned native/Wasm finite suite has
47 cases, plus one bounded corpus case on native. The generated corpus manifest
retains its 256-file and 8 MiB limits; record its exact migrated file/byte totals
and assertion counts with the current run. These case counts are source-derived,
not an execution result. Use the existing production 60-second supervisor after
independent exact-source/workload admission; configuration and build timing
remain separate.

## Qualified module prerequisite: admitted fixed workload

`tool language-modules` and `tool wasm-modules` route seventeen fixed pico_unit
cases through the existing `test_language --modules` dispatcher. The complete
workload constructs 112 catalogs with 196 source input entries, at most 196 source
lowering/finalization requests, 40 programs, 74 explicit event dispatches and ten
program steps. Catalog discovery, specialization and effect analyses are
additional bounded passes. Each preparation has at most five sources and two
libraries; at most three collections and two programs coexist. Event and step
calls retain their 1000-step arguments. Cases cover the original scroll and
ownership scenarios, imported generic and constant ownership, the 32-pair float
metadata matrix, incremental reuse and invalidation, retained library state,
task-state rejection and empty compile bindings. These source-derived counts
are not a current execution result. Each selected route or aggregate retains one
production 60-second deadline. The earlier seven-case workload and results below
are historical; see the [original coverage boundary](module-composition.md#fixed-workload-and-validation).

The original six-case checks at `072be745a8f5c73e14c1e07b0a1704c4de8555a8`
passed native/Wasm 6/348 (0.168/0.227 s), navigation 29/329 (0.183 s), and
incremental preparation 13/235 (0.430 s). Those are historical results: review
found a nested-helper collision and superseded the earlier approval. The new
regression-only tree `3ce898152de5e23316f3a6e144b0460c0d6f6ab6` failed only that
seventh case, at the intended `Scoped.Helper` duplicate diagnostic, with seven
cases/350 assertions in 0.179 s. Native ASan/UBSan remained enabled; LeakSanitizer
cannot scan this environment, so leak checking is unavailable, not passed.

Exact correction `e3749f9fae281a099c553d02fbbead80fe65467b` passed independent
review and five coordinated checks. Native/Wasm module routes each passed 7/438
in 0.204/0.251 s. Complete language (29 commands) passed 389/36,402 in 23.985 s;
complete Wasm (17 commands) passed 233/32,519 in 4.223 s. Exact-source
`native-tools`, including its own `policy_worker`, passed 116/29,566 in 21.699 s.
Each check ran once, serially, and exited 0. Source/artifact hashes matched before
and after; all three `-j2` build currency checks reported no work. No prior group
member was removed. The native language group includes program/imported state,
publication, navigation and incremental coverage. Each complete group retained
one shared production 60-second deadline, with configuration/build time separate
and no chunking or failure suppression. These are measured full commands, not
sums of old timings. This documentation follow-up preserves tested product,
test, policy and fixture bytes and performs no new runtime.
`wasm-format` remains a pre-existing standalone gap; this does not claim every
admitted selector is covered. See the linked proof and
[machine-readable historical evidence and current status](module-composition-validation.json).
No full-L07, application-scale, benchmark, or scheduling claim follows;
full/quarantined gates remain disabled, not passed.
The explicit platform merge hold remains; no GitHub approval or resolved-thread
status is asserted by this local validation.


## Selected runtime attribution: admitted fixed workload

`tool language-runtime-attribution` and `tool wasm-runtime-attribution` run the
same six C/pico_unit cases through the existing language dispatcher. Each occurs
once in its ordinary native-language or serial-Wasm group. This does not add a
second copy to the separate native `test_tools` selector group.

The fixed envelope is 44 copy API operations, ten scoped copies/VM calls, three
intern worlds with 32 unique edits and 32 repeats, four task owners with eight
cycles and four boundaries each, six native/five Wasm backend attempts, and
5,128 paired invocations. Tiny instruction/context budgets and the 512 KiB trial
storage cap are unchanged. There are no new frames, GPU/browser/network workers
or environment-controlled repetitions. Source review and exact artifact
validation preceded measurement; all commands retain their original shared
60-second production-supervisor deadline.

The six cases passed with 69,013 native assertions and 69,000 Wasm assertions.
The integrated complete language/Wasm groups, native-tools and installed
compiler-free prepared consumer also passed. Exact source, build/SDK boundaries,
raw timing samples, log hashes and limitations are in
[the validation record](evidence/runtime-attribution/validation.md). Counters
cover selected copy and backend operations; they are not a global allocation
census or a whole-application performance/retention claim.

### Private owner retention

The retained-ownership simplification reuses the admitted language, source-edit,
query-program, publication and prepared selectors. Finite VM/compiled cases cover
behavior-attachment isolation, registration isolation, retained helper/import
values, row removal/reentry, zero matches and reload/restart. Query regressions
keep ordinary locals independent across waits and cancel only the removed
structural match. They do not introduce engine frames, GPU work or sleeps.
Syntax and authoring cases reject `retain(system)` and keep plain `retain` in
system roots and helpers. Historical migration cases preserve behavior/system
owner retention but reject per-match system state before replacing files.
All commands retain the existing production supervisor's 60-second ceiling.

### Name-first retained bindings (historical project version 25)

The retention increment extends existing admitted selectors, without adding a
new executable or increasing the 60-second supervisor ceiling. Independent
review covered the grammar, semantic kinds, authoring equivalence and migration.

- `language-program`: three finite runtime cases on VM and the compiled backend
  check per-owner helper retention, system versus query-row ownership, copied
  values, membership reentry and publication/restart identity. Each dispatch has
  a 1000-instruction budget. Two migration cases use small literal sources and
  one temporary directory to check execution, idempotence, metadata remapping
  and refusal before staged replacement.
- `language`, `language-semantic`, `language-format` and `language-navigation`:
  accepted/rejected binding forms, ordinary value types, storage restrictions,
  formatter/minimizer ownership preservation and source intelligence. Numeric
  inference tests keep `int`/`float` annotations when removing them changes type.
- `source-edit`: one additional case uses exactly three small sources to check
  compact inferred retained retyping at behavior, callable and system scope.
  It has no runtime execution, subprocess, network or GPU work.

Existing implicit root lifetimes are unchanged. These checks do not establish
the later unified system syntax, queryable embedded components, packed storage,
or Studio/browser acceptance.

### Redundant root-state canonicalization

`language-root-state` and `wasm-root-state` select two finite C cases with five
fixed source strings, at most 137 UTF-8 bytes each. They check behavior/library/
system root storage, distinct callable storage, system shared storage, exact
declaration and read ranges, stable symbol identities, comments, idempotence,
formatter token preservation and invalid-source refusal. The full successful
path makes six explicit semantic analyses, seven minimizer calls and two
formatter calls. The minimizer's existing internal oracle rounds are unchanged.

The format dispatcher includes these two cases and the package separator case:
47 finite cases, plus the existing bounded corpus case on native. Wasm has no
corpus file dependency. No catalog, generated source, runtime frame, world,
subprocess, network or GPU workload is added. Use the unchanged production
60-second supervisor; independent exact-head source and artifact admission is
required before a runtime slot. Configuration and compilation are separate from
test execution. These SDK checks do not establish CLI/Studio integration or a
caret map across semantic rewriting.

### Authored record project publication

`python3 tools/test_policy.py tool language-record-publication` is a standalone
seven-case CPU route. It prepares a two-source project catalog with a declaring
record library and an importing behavior, then stages and commits both sources.
The BYTECODE and SLJIT variants cover preparation/code generation and canonical
publication, not authored-function execution. The fixture has zero authored
calls, attachments, event dispatches, frames, child processes or network work.
It also checks independent ownership, 31 fixed catalog/analysis conflicts,
qualified foreign-name separation, and three duplicate prepared tables through
both writer and reader. Two additional source-only cases cover deduplication of
all thirteen scalar default kinds, rejection after changing each default
(including signed floating-point zero), and preservation of a native nested
field's nominal layout. Prepared ID/name uniqueness stays strict.

The redesigned fixed green path has nine parses, sixteen declaration analyses,
at most twenty-five full analyses, 52 lower calls, four module finalizations, two source
stages/commits, and eight direct prepared-table codec entries. The two stages
serialize two unique modules and one import edge each. These are source-derived
bounds, not a measured pass. The route remains outside aggregates and requires
updated independent exact-source admission for the redesigned workload before
execution.

`language-record-publication-repro` runs only the first case's BYTECODE half
with the same success expectation. On the fixture-only baseline it must fail
at source staging with `duplicate prepared type requirement`, before commit.
That red invocation has two parses, eight declaration analyses, at most ten full
analyses, two lower calls, two module finalizations, one stage and no authored
calls. Red and green runs each use one production 60-second deadline with
build/configuration kept separate. This focused evidence does not establish
full Studio startup, rendering or native-catalog-only helper coverage.

## CUT-79 inferred string arrays: fixed standalone regression

`tool language-inferred-string-arrays` selects two C/pico_unit cases in the
existing language driver. This new source-only workload requires independent
exact-source and resource admission before its first supervised invocation.
Registration is not admission. It is not included in a native or Wasm aggregate.

The first case parses/analyzes 22 inline documents once each. It covers direct
and parenthesized literals, a named-constant alias chain, local/state/return
inference, name storage, contextual/empty arrays, unchanged numeric defaults,
explicit literal-union acceptance/rejection and indexed ordinary strings.
Literal expressions and named scalar constants retain their per-value facts;
explicit array/union constraints remain strict. Negative controls check the
diagnostic category and a range overlapping the offending source token.

The second case prepares two fixed twelve-glyph sources, inferred and explicitly
contextual, once each for BYTECODE and SLJIT. Each preparation has one named
constant initializer evaluated by the existing lower.c BYTECODE path with its
unchanged 100,000 budget. A separate run-counter scope requires exactly one
BYTECODE entry for that initializer. There are four such initialization entries,
distinct from the 48 direct Lookup calls (12 indices per module). Each direct
call has budget 2,000 and must record exactly one selected backend entry, no
other backend entry, no inspection entry/fallback and no counter overflow.
Module backend identity is checked separately; SLJIT is never replaced silently.
A Wasm build rejects this native runtime case before preparation.

On full success the source-counted totals are 26 parses/analyses, four lowers,
four finalizations, four initializer BYTECODE entries, 24 direct BYTECODE entries,
24 direct SLJIT entries and 445 assertions (156 semantic plus 289 runtime).
The fixture reports attempted work on failure too. Only one module, activation
and result is live at a time. Teardown is registered with pico_unit and releases
all fixture-owned objects after REQUIRE returns, before the next case/module.

Every inline source is at most 1 KiB and semantic source text totals less than
16 KiB. There are no files, workload children, engine frames, GPU operations,
network requests, sleeps, benchmarks or environment-sized loops. Run once under
the unchanged production supervisor's shared 60-second deadline including
setup, all preparation, calls and cleanup. Configuration/build timing is separate.
A timeout does not authorize splitting the workload or repeating until fast.

`tool language-inferred-string-array-repro` is an optional semantic-only route
for the first specimen, using the same expected-success assertions. A successful
patched run has one parse/analysis and eight assertions, with no lower,
finalization or direct call. An unpatched baseline should fail analysis with
eleven compatibility diagnostics. A separate baseline run is not mandatory;
any invocation needs its own admission and cannot mutate a preserved donor.

This coverage is frontend inference and native BYTECODE/SLJIT evidence only.
It does not establish generated-C, actual Wasm/browser, Studio UI, performance,
numeric-foundation or full-suite coverage. Wider affected-contract checks remain
an integration-review decision. Source-only registration records no measured pass.


## Tooling parent C++ language activation

The existing `desktop-dependencies` C/pico_unit selector adds one finite case
with four configure-only fixtures: a pure C runtime consumer with an invalid
C++ compiler path, a child-enabled scheduler with a parent C consumer, a
child-enabled renderer with a parent C++ consumer, and an imported renderer.
The C++ consumers reuse the existing `test/draw_backend.cpp` pass-through shim;
no new C++ logic or standalone executable is added. The fixtures generate real
Ninja rules through the production tooling language include. They do not build
the fixture targets, fetch dependencies, execute fixture code or use a GPU.
CMake's compiler identification/try-compile subprocesses remain part of each
bounded configuration child.

The whole existing selector now has eight C cases and twenty direct children
(previously seven/sixteen). Its existing ten-second child limits and one shared
production 60-second route deadline remain unchanged. The added case performs
four `REQUIRE` checks, one per successful complete configuration/generation.
Parent C++ activation follows actual scheduler/render targets, leaving pure C SDK
consumers alone. The OpenGL case relocates discovered roles on Linux and asserts
that other hosts reject the same absolute paths as unowned. The complete selector
passes 8 cases in 6.8s on macOS. The 60-second ceiling covers the entire desktop
selector, not each case separately.
### Snapshot-to-reader composition

The `python3 tools/test_policy.py tool language-snapshot-reader-chibi`
selector uses the existing signature-reader pico_unit owner and language
dispatcher. It does not add an executable or expand the old signature/snapshot
selectors. `analysis_state.TreeForDocument` converts the actual retained snapshot
forest into the existing `signature_reader.Tree.Raw`, carrying incoming fields,
raw named/extra flags, text, local byte positions and the explicit local syntax
root. Nonnegative topology links subtract the document's `node_start`; negative
sentinels remain unchanged. Rejected snapshots expose only parsed retained
prefixes. `DocumentsForPackage` preserves graph-member order, which is sorted by
path, in both directions;
nonmember source files remain `-1`, and a rejected graph has no package map.

Three fixed requests cover interleaved app/lib/app source order, reordered
lib/app/app source order, and a two-file rejected routing prefix. They prepare
one fourteen-source closure per backend (28 preparation parses); seven C oracle
trees and fourteen private raw parses make 49 total parses. Four private JSON
string decodes resolve the fixed import literals. The 64 top-level
calls are six snapshot builds, fourteen document-map calls, twenty tree-view
calls and twenty-four reader calls. Builds retain the existing supplied
three-million budget; adapters and readers use 500,000. Child activations inherit
and reset these arguments; they are not aggregate instruction limits.

Each request offers at most three files below 256 bytes, two packages and one
import. Trees stay within 128 nodes/document, 384 forest nodes and depth 32.
There are two package-local documents at most; map arrays have at most three
entries; rejected snapshots retain the existing fixed 256-entry cycle bank.
Reader fixture tables keep the existing 64-type/128-ID envelope,
two scopes/two symbols/one function, two definitions/two variants and zero
semantic import entries. A reading has at most four publications/new types,
uses and diagnostics. Eight retained-result projections stay below 128 KiB each.
Source bounds: reader sources below 48 KiB, snapshot/shared sources below
16 KiB; the State source is below 8 KiB and the combined closure is below
160 KiB. The exact source manifest records measured source bytes.

Each adapted full tree is compared with the current Tree-sitter-derived reader
input. For the six accepted document instances, direct and adapted inputs
produce the same complete authored reading, additionally checked against the
existing C reader oracle. The rejected parsed prefix checks full tree parity
without a reader call. Named/unnamed and extra/nonextra witnesses,
nonzero forest offsets, explicit package-local diagnostic document numbers,
unit text and constructor publication are included. Input snapshots retire
before the views are consumed, views retire before readings are compared, and
retained readings survive all compiler owners. No reparse occurs in the adapter;
C parser/reader calls are fixture oracles only. No runtime files, processes,
services, GPU work, new semantic native calls, or compiler completion result is
introduced.

The selector passes 3 cases / 61,885 assertions in 3.1s on macOS under one
production 60-second deadline; configuration/build time stays separate. Failure
to fit the source/row limits or deadline is a blocker; do not increase a cap or
split the selector to evade it. Header environments, semantic sidecars, body
analysis, exports, lowering, artifact assembly and `Checked` remain
unimplemented here.
