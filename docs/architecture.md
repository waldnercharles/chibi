# Architecture

Chibi separates executable source, prepared definitions, live execution and
authoring views. The engine repository owns their shared C implementations and
SDK contracts. Studio is a separate application built against an installed SDK.
Game repositories own composition, rules and tuning; `chibi-examples` owns labs
and sampled content. Engine regression tests use focused fixtures.

## Source and execution authority

Project format 10 uses `.chibi` as complete executable source for behaviors,
callable libraries and systems. Declarative scenes, prototypes, events, shaders,
models and fields use KDL; UI uses RML and RCSS.

`Chibi::language_syntax` retains source bytes and Tree-sitter syntax snapshots.
`Chibi::language` resolves typed declarations, symbols, expressions, events,
queries, state and bindings against an injected capability catalog.
`Chibi::compiler` lowers these records directly into verified executable modules.
Invalid drafts can supply authoring diagnostics and recovered facts, but cannot
be lowered or replace a valid applied generation.

`Chibi::capabilities` owns neutral signatures, stable identities, type and effect
metadata. Native packs share declarations with their presentation adapters.
Applications explicitly compose immutable native tables and provide actual
kernel, query, command and service implementations. Names resolve during
analysis; prepared linking validates identities and ABI requirements. Graph
registration is not a capability-discovery mechanism for source execution.

`Chibi::execution` owns verified instructions, VM and optional SLJIT backends,
linked programs, activations, state, suspension and inspection. Immutable modules
retain imports and source origins independently of syntax and editor objects.
Backend code and mutable instances have separate lifetimes. Persistent formats
store identities and compatibility requirements, not process callback pointers.

Authoring graphs project resolved source records. Graph edits produce source
edits, followed by analysis and preparation before adoption. Optional
`.chibi.meta` holds layout and debugger presentation; it supplies no executable
values or connections. Native shader/model/field graphs have separate compilers.
Their producer API rejects executable behavior domains and metadata-only nodes.

## Package boundaries

Each SDK target owns its sources, public headers and explicit link dependencies.
Build headers are scoped under `public/<owner>` and installed headers under
`include/chibi/<owner>`. Private headers remain with the implementation.
Consumers do not add the engine's `src` directory to their include paths.
[`sdk/contracts.cmake`](../sdk/contracts.cmake) checks declared source and link
ownership; package validation checks exports and configured build evidence.

| Targets | Responsibility |
| --- | --- |
| `types`, `ecs`, `execution_contract`, `capabilities` | Values, schemas, entity storage and neutral contracts |
| `language_syntax`, `language`, `compiler` | Source snapshots, analysis and direct lowering |
| `execution`, `behavior_runtime`, `scheduling` | Programs, source registrations and scheduled execution |
| `authoring`, `source_catalog`, `source_presentation` | Shared source services, project generations and projections |
| `graph_model`, `graph_authoring`, `graph_compilation` | Graph documents and nonbehavior native asset preparation |
| `project_compilation` | Offline project preparation and portable artifact production |
| `engine`, `player`, optional native/runtime targets | Simulation composition and prepared project consumption |
| `render`, `project_preparation` | Desktop rendering and Studio preview integration |

Compiler configuration can omit desktop dependencies. Prepared runtime/player
configuration can omit compiler, syntax and authoring packages. Rendering is a
host dependency; it does not become a dependency of the execution kernel.
The standard engine composes available simulation packages, while world options
select which physics, material, particle, visibility, audio, UI, input and visual
motion services are instantiated. Individual capability packages remain usable
through explicit host composition.

## Generations and runtime lifetimes

Source collections retain declaration snapshots, syntax, semantic records and
finalized modules. Working drafts and applied collections remain separate.
Source publication preflights and stages the complete final registration set,
including imports, events, schemas and required services. Commit performs no
dispatch or Begin. Failed staging preserves active programs, attachments, state
and pending work. Multiple worlds must all stage successfully before any commits.

Registries own live programs; ECS owns entity generations and component storage.
Query membership controls system owner lifetimes. Membership loss and entity
destruction cancel retained work. Component views are reacquired across
suspension and structural boundaries. Compatible state survives replacement by
stable identity and type; a restart deliberately resets it.

Programs copy service binding rows and names and borrow the host service values.
Those values outlive staged and live programs. New publications use registry
defaults followed by explicit overrides. Replacements preserve existing bindings,
fill missing defaults, then apply explicit overrides. Applications explicitly
inject optional Studio capabilities and bind their Studio service; a content path
does not select editor capabilities in the SDK source factory.

Scheduling consumes verified immutable module access/effect summaries and
explicit phase/order. Safe source-only cohorts prepare query membership and Begin
serially, run prepared activations in workers, then finish serially after joining.
Unsafe effects, mixed native/source phases, profiling and inspection retain serial
execution. Runtime code does not infer parallel safety from a system name or role.

## Prepared artifacts and products

`chibi-compile` prepares executable modules, scenes and declared resources without
a display host. `chibi-player` owns its loop and consumes prepared artifacts with
an explicit native linker and resource host. Resource staging validates the scene
and complete dependency closure before publication or Begin. Portable readers
validate schemas and format versions, preserve prior outputs on failure, and
charge retained decoded storage against a shared budget. Mutable execution state,
backend code and separately bounded parsing scratch have distinct accounting.

The compiler-enabled SDK, compiler-disabled runtime SDK, deployed player and
Studio distribution are separate packages. SDKs contain scoped headers, libraries
and installed CMake targets. Player deployment contains its executable, shaders,
licenses and identity manifests. Studio owns its starter and shell resources;
games and examples are not implicitly discovered or shipped from `projects/*`.
There is no MCP product.

Package manifests record complete file inventories, build identities, project
format, compiled artifact versions/source hashes and ABI/platform requirements.
Studio pins the SDK captured at configuration. A game's `.chibi-engine.json` pins
its copied player deployment and source runtime SDK. Compiler and runtime builds
may have different engine IDs; their required runtime headers, formats, schemas
and platform contracts must agree. See [native packages](native-package.md) and
[runtime authoring](runtime-authoring.md) for the installed interfaces.

## Acceptance limits

These are implementation contracts, not a claim that every product path has
passed functional acceptance. Installed-consumer builds establish dependency
boundaries; package checks establish integrity. Rendered player behavior, Studio
interaction and browser execution require their own reviewed captures. Current
evidence and outstanding requirements are tracked in [PLAN](../PLAN.md).
All tests retain the [60-second admission policy](testing.md#temporary-60-second-policy);
quarantined coverage is not treated as passed.
