# Authoring the Studio editor

Studio is an authored Chibi application. Its RML defines structure, RCSS defines
appearance, and `.chibi` owns editor commands, selection, panel state, preferences
and workflows. The Studio repository consumes an installed SDK through a
`LANGUAGES NONE` CMake project. Shared compiler, document, platform, rendering
and input capabilities belong in the engine. Native engine controls provide
mechanics available to both games and tools.

## Source ownership

The application is `chibi-studio/projects/studio`. Its `StudioApplication`
prototype mounts the authored behaviors; `StudioRoot` composes one instance of
each panel. Add shared UI compositions under `content/ui`, behavior under
`content/behaviors`, and typed events/components under their matching content
directories. Use these same components for new panels. The editor's design rules
are in `docs/prototypes/studio/DESIGN.md`, `docs/editor-ui-references.md` and
`docs/editor-graph-references.md` in the engine, and Studio's UX laws skill.

Keep editor commands and workflow decisions in authored code. An SDK service
should expose a reusable operation with explicit inputs, bounded resource use,
ownership, failure behavior and a meaningful validation seam. It should not
know Studio panel names or own editor selection. The RmlUi C++ binding remains
a pass-through adapter.

Historical native implementations and their extracted origins remain available
through Studio's Git history and retirement inventory. They are reference
material, not current component APIs or passing acceptance evidence.

## Build from installed products

Package the engine, then build the authored application:

```sh
make package-sdk SDK_PREFIX=dist/sdk
make studio-build STUDIO_SOURCE=/absolute/path/to/chibi-studio STUDIO_SDK_PREFIX=dist/sdk
build-studio/bin/chibi-dev /absolute/path/to/workspace
```

Studio's compiler, generic `chibi-author` host, SDK metadata and resources must
identify the same installed package. `chibi-compile --authoring` selects the
explicit authored-tool preparation profile; ordinary game preparation retains
its default budget. The artifact cannot request a larger host budget itself.

Native launchers resolve installed products beside themselves. Without an
explicit workspace, they search working-directory ancestors and otherwise open
Home. Browser distributions use matching `package-web-sdk` and
`package-web-author` products, the authored Studio artifact, editable source and
an explicit initial-workspace inventory. The compiler Worker has its own source
filesystem and lifetime. Browser and native executable identities are distinct.

## Shared components and event delivery

Compose shared fields, menus, pickers, dialogs, notices, lists and layouts in
RML. Use RCSS design tokens and `data-*` interaction state. Numeric fields,
colors, booleans and references use controls matching their reflected types.
Choice menus include all values, with at most seven visible rows and scrolling
or search for the remainder. Keep human tasks visible and put diagnostics behind
explicit disclosure.

Reusable native controls, such as `code-editor`, `number-field`, `color-field`,
`splitter`, `choice-list`, `tab-list`, `line-chart` and `texture-view`, provide
shared input or rendering mechanics. Their composition and application policy
remain authored. `texture-view` accepts bounded `kit:` keys bound in its render
context; dynamic ordinary image URLs still require an explicit prepared image
resource closure.

UI events have a shared bounded scalar argument interface. Complete scalar
payload tuples are explicit, including names that otherwise derive from the
DOM event. Partial tuples retain automatic pointer/modifier/control values.
Preparation diagnoses argument counts beyond the shared bound. Use typed
`.chibi` events between behaviors, preserve request/revision correlation through
asynchronous work, and discard stale completions before updating current state.

## Documents, candidates and runtime views

The generic workspace host owns scoped services and their lifetimes. An inactive
host exposes Home/platform capabilities without scanning a user directory.
Opening a workspace creates document, resource, semantic, preview and runtime
services from an explicit root. Retirement releases dependent views before their
underlying documents, catalogs and render contexts.

`workspace_documents` is the canonical document/history store. Source, graph,
Properties and designer edits prepare pure candidates, obtain an authored
external-edit lease, and apply an exact-revision atomic group. Missing metadata
companions remain drafts until acceptance. Persistent new artifacts carry their
valid birth baseline. Save reports disk conflicts without dropping unsaved text.
Recoverable file deletion retains explicit recovery data until forgotten.

A workspace checkpoint preserves disk bytes separately from current and saved
document text, exact handles/revisions and grouped undo/redo. View state is
owned by authored behaviors. Checkpoint restore is transactional into a fresh
candidate. These are editor checkpoints, not a claim to restore arbitrary live
physics or external effects.

Preview services use private resources and frozen source generations. They do
not publish temporary assets into the editor's main catalog. Scene Play input
and target UI events are explicitly supplied by authored focus/capture policy.
View measurements expose their actual pixel ratio; pointer coordinates retain
the documented physical-pixel contract. Simulation timelines describe their
actual supported input scope and provenance, with UI effects distinguished from
gameplay input replay.

## Validation and evidence

A source compilation check proves syntax, types and preparation. CPU service
checks establish their asserted contracts. Startup establishes initialization.
Rendered input checks establish only the exercised interactions. Package checks
establish inventory and identity. Browser acceptance establishes the combined
browser product. Keep these scopes separate in reports.

The generic host's bounded UI replay uses ordinary hit-tested pointer, key and
text input. It asserts actual visible/data results and records the request,
prepared artifact, installed SDK, backend, dimensions, timestep and capture
identity. See `docs/testing/ui-replay.md`. Scene and asset evidence similarly
identify their source generation, actual renderer and produced artifacts.

Follow `docs/testing.md` and the production supervisor's 60-second ceiling.
Quarantined native/render/Studio aggregates remain unpassed and must not be
recreated as a series of chunks. Historical test reports, source-only checks
and an unexecuted fixture never establish current rendered acceptance.
