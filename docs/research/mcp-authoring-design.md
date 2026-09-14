# MCP authoring design

Researched September 10, 2026. This note compares the MCP protocol, Blender's
first-party APIs and MCP server, and a representative community Blender MCP.
It separates the chosen Chibi slice from useful follow-up ideas.

## Chosen slice

The first authoring surface should add three tools to the existing local
stdio/controller MCP described in [`docs/mcp.md`](../mcp.md):

| Tool | Contract |
| --- | --- |
| `search_nodes` | Read-only registry search by text or behavior/shader domain, with bounded pages and optional typed pins. |
| `edit_graph` | Atomic semantic edits for behavior, event, library, material, shape, and shader-library graphs. |
| `preview_shader` | Compile and render a material or shape asset in `chibi_render_ctl`, returning a bounded PNG and optional generated source. |

`edit_graph` uses the shared graph model and serializer through
[`graph_edit.c`](../../src/automation/graph_edit.c), then the native controller
publishes the serialized clone. It does not ask the model to write graph JSON
or replay UI actions. `validate_asset` remains the CPU validation entry point;
material and shape validation already lower through the shader graph path. GPU
compilation, image output, and optional generated source belong together in
`preview_shader`, so a separate `inspect_shader` tool is not required for this
slice.

The relevant existing seams are [`graph_edit.h`](../../src/automation/graph_edit.h),
[`compile.c`](../../src/graph/compiler/compile.c),
[`shader_graph.h`](../../src/graph/shader/shader_graph.h), and the current
[`MCP controller`](../../src/automation/controller.c).

## Tool contracts

### `search_nodes`

The current contract accepts `query`, `domain` (`behavior` or `shader`),
`limit` (default 10, maximum 50), `offset`, and `include_pins` (default true).
It matches text against node names, categories, and keywords. Results contain
node name, category, purity, and optional typed input/output pin descriptors,
plus `total`, `offset`, `count`, and `has_more`. It does not yet filter by pin
type, container, or capability, and uses offsets rather than cursors.

### `edit_graph`

The request selects an asset by `path` or by `kind` and `name`, optionally
selects a graph with `graph_id` or `graph_name`, and supplies an ordered
`operations` array. The current exposed operation names are:

- `add_node` and `remove_node`;
- `set_input` for a typed input default;
- `set_node` for event names, references, disabled state, and inline source;
- `add_pin` for dynamic signatures, `set_ramp` for shader color stops, and
  `set_bounds` for shape half extents;
- `connect` and `disconnect`;
- `add_graph`, `add_variable`, `set_variable`, `add_variable_node`, and
  `add_self_node`.

`add_node` accepts a registry type, optional request-local `ref`, initial
`inputs` and declaration reference fields. Function references
refresh their call pins through the shared graph model.

Node selectors resolve to a numeric ID or a request-local string ref. Pin
selectors accept a name or zero-based index; duplicate names fail as
ambiguous. A node ref created by `add_node` can be used by later operations in
the same batch, and the result returns node, graph, and variable refs.
Function creation also exposes `<ref>.entry` and `<ref>.return` aliases for
authoring complete signatures and bodies in one batch.

An existing-asset request requires a 16-character lowercase `expected_hash`.
`create: true` creates a new empty asset (with a shader output seeded for
material or shape) and omits the expected hash. `dry_run: true` applies the
batch to a clone and validates it without publishing. Apply performs the same
validation and publishes the serialized result once through the locked,
temporary-file/rename writer. The first slice is offline and does not claim to
create a live editor undo unit. Shared graph arrangement assigns node positions
after every successful edit. The MCP edit API has no coordinates or layout controls.

The current result contains asset identity, `before_hash`, `after_hash`,
`changed`, `dry_run`, `published`, `graph_id`, `operation_count`, created
request refs, and compiler/shader diagnostics. It does not yet return a
semantic diff with `created`, `removed`, or `reconnected` records.

Validation, schema, type, cycle, and conflict failures save nothing. Compiler
diagnostics identify graph, node, and pin IDs with rule names and messages;
shader diagnostics identify the source node. Templates and a richer semantic
diff remain follow-up proposals.

### `preview_shader`

The request accepts `path` or `kind`/`name`,
`width` and `height` from 16 through 512, finite `time` within
plus or minus 3600 seconds, and `include_source`. The MCP adapter launches the
separate `chibi_render_ctl` process. That process reloads the content asset,
links shader libraries, validates and compiles it, and renders a bounded PNG.
Shader-library assets report that they must be previewed through a material or
shape consumer; they have no standalone image preview. Generated GLSL is
optional and capped at 65,536 bytes.

The MCP result returns image content plus a short summary and structured
metadata with the image bytes removed. It reports validation, compile, image,
timeout, cancellation, and process errors. CPU graph validation remains
`validate_asset`; the preview is the separate GPU/image check.

## Protocol and token economy

The MCP specification distinguishes model-controlled tools,
application-controlled resources, and user-controlled prompts. See the
[server overview](https://modelcontextprotocol.io/specification/2025-06-18/server/index),
[tools](https://modelcontextprotocol.io/specification/2025-06-18/server/tools),
[resources](https://modelcontextprotocol.io/specification/2025-06-18/server/resources),
and [prompts](https://modelcontextprotocol.io/specification/2025-06-18/server/prompts)
specifications.

Apply the protocol as follows:

- Tools perform bounded queries, edits, and previews. Use standard tool
  annotations and the native safety descriptor to describe read-only and
  destructive behavior.
- Results use `structuredContent` with a short text summary for clients that
  do not render structured content. Define `outputSchema` for stable fields.
- The chosen `search_nodes` contract uses `limit`, `offset`, and
  `include_pins`; its maximum page is 50. The graph batch is capped at 128
  operations, and preview dimensions are capped at 512 pixels. Cursor-based
  pages, type/capability filters, full diffs, and resource links are follow-up
  options rather than current fields.
- Preview images use image content. The adapter removes image bytes from its
  structured metadata and keeps generated source opt-in and capped.
- Errors distinguish invalid requests, stale hashes, and valid graph/shader
  validation failures. Return one actionable record per issue.

The [MCP tools specification](https://modelcontextprotocol.org/specification/2025-06-18/server/tools)
defines pagination, output schemas, structured results, image content, and
`isError`, while the [progress](https://modelcontextprotocol.org/specification/2025-06-18/basic/utilities/progress)
and [cancellation](https://modelcontextprotocol.org/specification/2025-06-18/basic/utilities/cancellation)
specifications cover long previews.

## Blender evidence

Blender's Python quickstart documents named data blocks, owned creation/removal
collections, and the context and mode checks that affect operators. It is
evidence for using a data-oriented semantic API with explicit references, not
for copying Blender's scene model. See the [Blender Python API
quickstart](https://github.com/blender/blender/blob/main/doc/python_api/rst/info_quickstart.rst)
and [NodeTree API](https://docs.blender.org/api/current/bpy.types.NodeTree.html).

The [official Blender Lab MCP](https://www.blender.org/lab/mcp-server/) is a
first-party lightweight bridge to that API. Its audited source provides
[inspect-first instructions](https://projects.blender.org/lab/blender_mcp/src/branch/main/mcp/blmcp/data/prompts.yml),
[compact object summaries](https://projects.blender.org/lab/blender_mcp/src/branch/main/mcp/blmcp/tools/get_objects_summary.py),
[bounded API-doc search](https://projects.blender.org/lab/blender_mcp/src/branch/main/mcp/blmcp/tools/search_api_docs.py),
and [targeted screenshots](https://projects.blender.org/lab/blender_mcp/src/branch/main/mcp/blmcp/tools/get_screenshot_of_area_as_image.py).
It also contains a generic code execution tool, which is a separate capability
from its typed inspection tools.

The popular [ahujasid/blender-mcp](https://github.com/ahujasid/blender-mcp) is
a separate community project and says it is not made by Blender. It shows a
TCP bridge, bounded scene summaries, viewport images, and a Blender main
thread queue. Its generic Python execution and material/object helpers are
useful comparison points, but they do not define Chibi's asset schema.

The evidence supports compact semantic inspection, filtered documentation or
registry lookup, targeted visual feedback, and serialized access to a live
runtime. It does not establish a need for raw graph dumps or a second generic
mutation language.

## Follow-up proposals

These are useful extensions, not requirements for the chosen three-tool slice:

- Add cursor and pin type/container/capability filters to `search_nodes`.
- Add server-owned templates, a dedicated `create_asset` operation, and a
  richer semantic diff to `edit_graph`.
- Add resource templates for full graph snapshots, registry pages, diffs, and
  generated shader source. MCP resources are application-controlled and suit
  large reusable context.
- Add user-selected prompts for authoring or repair workflows after the tool
  contracts stabilize.
- Add source line ranges mapped to shader node IDs if generated source becomes
  a frequent repair aid.
- Add subscriptions or change notifications if the MCP later attaches to a
  live editor.

## Verification

The first implementation should verify:

- create, add, connect, set input, set node reference, add graph/variable,
  and remove for behavior, material, shape, and shader-library assets;
- unknown nodes, ambiguous pin names, incompatible pins, cycles, invalid
  defaults, malformed shader inputs, and stale hashes fail without changing
  the asset;
- `dry_run` returns hashes and refs without publishing, while apply produces
  one atomic file update;
- `validate_asset` catches CPU graph/shader errors and `preview_shader` catches
  GPU compilation errors while returning an image;
- concurrent requests against one asset produce a conflict rather than a
  silent overwrite; and
- response sizes and model turns improve against a raw graph JSON baseline.

Validate the contract with a second scene or fixture that uses different
content, so the tools represent reusable engine capabilities rather than one
arena's composition.

## Sources

- [MCP server overview](https://modelcontextprotocol.io/specification/2025-06-18/server/index)
- [MCP tools](https://modelcontextprotocol.io/specification/2025-06-18/server/tools)
- [MCP resources](https://modelcontextprotocol.io/specification/2025-06-18/server/resources)
- [MCP prompts](https://modelcontextprotocol.io/specification/2025-06-18/server/prompts)
- [MCP progress](https://modelcontextprotocol.io/specification/2025-06-18/basic/utilities/progress)
- [MCP cancellation](https://modelcontextprotocol.io/specification/2025-06-18/basic/utilities/cancellation)
- [Blender Python API quickstart](https://github.com/blender/blender/blob/main/doc/python_api/rst/info_quickstart.rst)
- [Blender NodeTree API](https://docs.blender.org/api/current/bpy.types.NodeTree.html)
- [Blender Lab MCP announcement](https://www.blender.org/lab/mcp-server/)
- [Blender Lab MCP source](https://projects.blender.org/lab/blender_mcp)
- [Blender Lab MCP code execution tool](https://projects.blender.org/lab/blender_mcp/src/branch/main/mcp/blmcp/tools/execute_blender_code.py)
- [Community `ahujasid/blender-mcp`](https://github.com/ahujasid/blender-mcp)
