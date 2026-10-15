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
make test-native         # every other CPU test_tools selector with checked-in inputs, one 60-second deadline
make test-materials      # the material simulation selectors, one 60-second deadline
make test-projects       # project, asset import and chibi_ctl selectors, one 60-second deadline
make test-language-all   # every test_language selector, one 60-second deadline
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
the drivers `make test-build` produced and that Release `chibi_ctl`; set
`CHIBI_TOOLS_BUILD_DIR`, `CHIBI_LANGUAGE_BUILD_DIR` or `CHIBI_CTL` to use another
build.

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
materials, input and entities). `test_tools --feature-coverage <project>` compares the engine
catalog (components, native functions and events) with what the project's
scripts, prototypes and scenes use, prints each gap, and runs every scene for
four frames through `chibi_ctl`'s `run_scenario`, failing on any behavior
failure. Every prototype, behavior, library and system must be reachable from a
scene: placed or attached there, spawned or called from source that runs, and
only that source counts. A spawn reaches the prototype it names directly or
through a value, such as a local, state or library parameter that holds the
prototype's name, as find references does. A scene entity with its own behavior list
runs that list in place of its prototype's; otherwise it and a spawned
prototype run the prototype's list. A prototype's children spawn with each of
its instances, so they are reached and run as instances too. Components count on these placed and
spawned instances, after the scene's overrides, and where running source reads,
writes or adds them, directly or through a generic call such as
`add_component("Heat")`; a system query alone matches nothing without
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
assertions in 0.828 seconds with ASan/UBSan. Neither selector loads a renderer or
runs engine frames.
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

`--fields` runs eighteen cases on field graphs compiled to field programs:
deterministic cos/sin, atan2 and exponentials against golden bits, primitives
that bake as they sample, parameters and compile errors, noise and
composition, row evaluation matching scalar samples, repeated inputs,
functions, multiple results, terrain chunks and connectivity, and streaming
tiles that restore and retry failed slices, plus linked-library publication
and rollback, source-library linking and constant parameter defaults, and library
updates that preserve valid and invalid field drafts, and source calls that retain
the named function and authored inputs after functions and parameters reorder.
The original thirteen cases passed 3,308 assertions in 0.36 seconds with
ASan/UBSan; these historical measurements do not cover the five added cases.

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

`python3 tools/test_policy.py tool tilemap-project` admits two project acceptance
cases when the tooling build includes asset authoring. It reads only the sibling
`chibi-celeste` checkout. A bounded local `git show` supplies the former Level
scene from commit `56ca9d486b93c15a504bf293dc722c6202cd2bf5`; the production KDL
parser compares all 1,472 cells and 230 former tile centres/frame indices, then
checks the four retained actors and one Room layer. Inputs are capped at 64 KiB
for the baseline, 16 KiB for Room and 4 KiB for Level. Three owned controller
children validate, prepare and run exactly two headless simulation frames with
five authored and fifteen live entities and no behavior failures. Each child
has the existing ten-second deadline inside the shared sixty-second supervisor.
No GPU or network is used. Logs and the baseline revision are retained under
`/tmp/chibi-publish-validation/evidence`; generated baseline, requests and prepared
artifacts are removed. This fixed workload is independently admitted for its
first supervised measurement; it does not admit arbitrary projects or frames.
The first supervised run passed both cases and 3,818 assertions in 8.270 seconds,
including controller startup, source preparation and the two simulation frames.

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
- `tool language-binding-schedule` and `tool wasm-binding-schedule` exercise four
  fixed binding-cache cases on both backends: module/owner generation changes,
  peer destruction, caller ordering, cycle rejection before writes, query-gate
  changes and dynamic dependency targets. Cache fallback uses 17 programs and
  1,025 independent constant binding rows, proving cache bounds are not execution
  limits. There are no frames, child processes or GPU work.
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
| `test-language-all` | Every `test_language` selector under one shared deadline. |
| `agent-test` | One admitted selector routed by `tools/agent.py`. |
| `test-list`, `agent-list` | Admission inventory without test execution. |
| `test-fast`, `test-full`, `test-slow`, `test-render`, `test-compiler-parity` | Retired or quarantined, disabled and not passed. |

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
order, and a zero-initialized arena. It passes 354 assertions in 0.06
seconds.
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


## External authored examples acceptance

The Examples repository owns `projects/atrium` and `projects/tetris`. Engine
unit and default CPU groups do not depend on that checkout. Run their explicit
CPU acceptance with an absolute Examples root and a matching Release controller:

```sh
CHIBI_EXAMPLES_ROOT=/absolute/path/to/chibi-examples \
CHIBI_CTL=build-release/bin/chibi_ctl \
python3 tools/test_policy.py tool examples-projects
```

`--examples-projects chibi_ctl` is the C tooling dispatcher route; `make
agent-test AGENT_SELECTOR=--examples-projects` uses the same supervisor. Missing
external roots or either project fail the cases; they never skip. Three cases
validate Atrium without warnings, validate and exercise Tetris rotation/drop and
restart, and verify blocked Hold ends its tick without changing the score. The
Tetris cases clone their project with copy-on-write and remove the copies after
each case. They run two 170-frame snapshots and one 22-frame snapshot, reporting
at most three entities and one behavior per snapshot, without rendering. One
shared production 60-second deadline covers both validations, all snapshots and
fixture setup; each child also has the existing 10-second tooling limit.
The inline language event, system cancellation and binding cases retain both
VM and compiled-backend coverage without reading the Examples checkout.
