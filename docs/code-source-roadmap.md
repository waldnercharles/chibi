# Canonical .code source

[CUT-70](https://linear.app/cute-engine/issue/CUT-70/replace-kdl-with-canonical-code-assets-and-first-class-graph-authoring)
tracks complete replacement of KDL as the authored asset format. Graphs are to
be built directly from `.code`, with text and visual editing using the same
native model. Behavior graphs are the first milestone. The goal also includes
other KDL-backed graph domains, scenes, prototypes, systems, scenarios and
project manifests. RML/RCSS markup remains a separate format.

## Current implementation

The JavaScript bridge under `tools/behavior-code` parses source and invokes
MCP graph edits. It persists an executable KDL graph and a JSON identity map.
The native loader skips `.code`; Studio does not invoke this frontend.
[CUT-45](https://linear.app/cute-engine/issue/CUT-45/deliver-behavior-scripting-through-the-existing-graph-runtime)
tracks that bridge and its remaining acceptance. It does not deliver canonical
source storage. Its recorded behavior-editor panel hold remains in effect.

## Native source contract

One shared C frontend outside `src/editor` serves Studio, the controller/MCP
and Native/WASM loading. Source parsing, reflected name resolution and graph
construction produce a candidate native model with diagnostics and source
ranges. Existing graph validation and runtime compilers consume that model.

`.code` owns authored content. Graph saves update the same source document.
IDs, layout and comments have explicit authoring metadata in the source
contract. Generated caches are derivable. Converted assets require neither
persistent generated KDL nor an independently edited `.code.map.json`.

The language needs an explicit typed node/connection form for graph topology
outside structured statements. It must preserve functions/macros, variables
and defaults, bindings, disabled nodes, IDs, edges, reroutes and comment boxes.
Raw KDL embedded in source does not complete the format migration.

Non-graph assets need declarative document syntax sharing lexical rules,
references, values and diagnostics. Invalid source retains the last valid
document/runtime generation. State declaration edits need defined runtime
replacement semantics.

## Start here

[CUT-71](https://linear.app/cute-engine/issue/CUT-71/build-the-shared-c-code-frontend-and-direct-behavior-graph)
is the next implementation milestone:

1. Specify behavior grammar and identity/layout metadata, including explicit
   graph forms alongside the current structured language.
2. Add a shared C source API under `src/assets`, with explicit ownership of
   parsed source and caller-owned candidate `behavior_asset` values.
3. Resolve through existing engine registries and construct graph models
   directly. No Node/MCP subprocess or textual KDL step is needed.
4. Implement canonical formatting and source reconciliation with stable IDs.
5. Expose the same API through the controller and verify Native/WASM builds.
6. Add focused C/pico_unit checks for parsing, diagnostics, model round trips,
   identity, failed edits, state lifecycle and existing runtime parity.

Relevant boundaries are `src/graph/model/behavior_asset.h`, `graph.h`,
`src/graph/document/serialize.h`, `src/assets/asset_source.h`,
`src/editor/documents/asset_code.c` and `src/editor/studio/studio_code.c`.
The frontend foundation can proceed before the deferred Studio panel work.

## Subsequent migration

Convert every asset in a domain, parse the emitted source, and compare native
semantic content and authoring metadata before changing its persistence path.
Unsupported topology must remain recoverable; conversion cannot silently omit
it. Complete each corpus conversion before removing its obsolete KDL loader,
serializer and files. Do not retain permanent dual authoring paths.

Breaking project loading requires incrementing `CHIBI_PROJECT_VERSION` and
updating every project manifest. Re-read the current version when implementing.
Manifest migration also changes `CHIBI_PROJECT_MANIFEST`, project discovery,
creation and package/install paths.

After behavior persistence, integrate Studio source/graph editing, drafts,
diagnostics, undo/save/live apply and navigation through the shared frontend.
Then migrate the remaining graph and declarative document domains. Remove the
old JavaScript bridge and identity sidecars when their replacements are complete.
