# Node editor references

This document is the node-graph counterpart of [Editor UI references](editor-ui-references.md). It covers the libraries and editors whose interaction contracts the RmlUi behavior graph editor ports. We port behavior, not code. Numbers were read from source or official docs on 2026-09-23; **(unverified)** marks the few that weren't.

What the graphs themselves should offer (nodes, conversions, compiler safety and automation) is in [Graph language: lessons from Unreal and Blender](research/graph-language-2026-09-23.md).

## Map

| Reference | Best at | What we take |
| --- | --- | --- |
| [xyflow](https://github.com/xyflow/xyflow) (React/Svelte Flow) | The general canvas: a framework-free core (`@xyflow/system`) with thin views | Core/view split, connection radius and three-state validity, bezier shape, 20 px wire hit area, auto-pan, fit view, one change path |
| [Vue Flow](https://github.com/bcakmakoglu/vue-flow) | xyflow for Vue; n8n's canvas | Controlled mode: edits arrive as changes the model accepts or vetoes |
| [thedmd/imgui-node-editor](https://github.com/thedmd/imgui-node-editor) | Native, Blueprint-style; the editor runs gestures, the host answers rules | Per-frame accept/reject of a candidate link with a reason, commit on release, dimmed incompatible pins, F cycles frame selection/all, pin-to-grid snapping, flow markers |
| [imnodes](https://github.com/Nelarius/imnodes) | Minimal immediate-mode node editing | Link started/dropped/created/destroyed events, hover radius 10, pick up a wire from a connected input |
| [Blender node editors](https://docs.blender.org/manual/en/latest/interface/controls/nodes/index.html) | The house style: dense, keyboard-driven | Zoom-aware socket radius, link-drag-search, insert node on link with auto-offset, knife cut, reroute sweep, frames, groups (Ctrl-G, Tab), mute, collapse, inline defaults, timings overlay |
| [Unreal Blueprints](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-editor-cheat-sheet-in-unreal-engine) | Exec plus data programming, debugging | Exec arrows and hollow pins, context-sensitive action menu, conversion nodes, reroute on double-click, comments, Q straighten, F9 breakpoints, watches, pulsing active wires |
| [ComfyUI](https://github.com/Comfy-Org/ComfyUI_frontend) (LiteGraph fork) | Large dataflow graphs, live execution | Gesture state machine with a distance-only drag threshold, mouse/trackpad detection, drop-on-node fallback order, delete-and-heal, typed fuzzy search, readable-font level of detail, run/error outlines |
| [Blockly](https://github.com/google/blockly) | Programming blocks, keyboard navigation | Shadow values: an input's edited default survives connecting and returns on disconnect; connection checker stages; `highlightBlock` |
| [tldraw](https://github.com/tldraw/tldraw) | Canvas camera and gestures | Screen-space thresholds, wheel/pinch model with a mouse/trackpad preference, interaction-level undo marks, edge scrolling, snapping in screen px |
| [Rivet](https://github.com/Ironclad/rivet) | Programming graphs with run inspection | Compatible/coercible/incompatible port highlighting, Ctrl on release to fan out, "not ran" dashed wires, run status per node |
| [n8n](https://github.com/n8n-io/n8n) | Workflow graphs | Connection radius 60, wire-drop opens a creator filtered by type, arrow keys walk the graph, item counts on wires, a "dirty" model for partial runs |
| [Node-RED](https://github.com/node-red/node-red) | Wiring gestures | Splice a node onto a wire, Ctrl+Delete heals wires, Shift-drag moves wires, Alt-drag slices, zoom focus locked per gesture |
| Rete, Baklava, Flume, Drawflow, reaflow | Web visual programming | Controls shown only while unconnected; Baklava's explicit conversion table; Rete's 200 ms history merge |
| Unity Shader Graph / Visual Scripting, Godot, Houdini, TouchDesigner | Engine graph editors | Fuzzy add with synonyms and Tab completion, dimming unused nodes, last value on connections, Tab search at cursor, node info on middle-click, animated live wires |
| [ELK Layered](https://eclipse.dev/elk/reference/algorithms/org-eclipse-elk-layered.html), Godot's arranger, dagre, d3-dag, libavoid | Automatic layout and routing | Layered layout with fixed port order as the spec for arrange; libavoid only for an optional orthogonal mode |

## Contract for the Studio graph editor

These are our decisions, drawing on the references above. Distances are screen dp unless stated; world units are graph coordinates.

### Camera

- **Zoom:**
  - Range 0.1–2.0.
  - Continuous and anchored at the cursor: `zoom · 2^(−Δy · 0.002)` per pixel of wheel delta, which is about ×1.15 per mouse notch (xyflow).
  - The focal point stays fixed for the whole wheel gesture; a gesture ends 150 ms after its last event (d3, Node-RED).
  - Fitting never zooms past 1:1 (n8n, Rete).
- **Scrolling zooms,** from a mouse wheel or a trackpad alike, as Blender and Unreal do. One event never more than doubles or halves the zoom.
- **Pan:**
  - The middle button always pans, even over nodes.
  - Space+drag pans.
  - Right-drag pans once it passes the threshold; a right-click that doesn't move opens the context menu (xyflow, tldraw, Unreal).
- **Frame:**
  - F frames the selection; pressing F again frames everything (imgui-node-editor). Home frames everything.
  - Fits use 10% padding (xyflow) and animate over 0.3 s easeInOutCubic.
- **Auto-pan:** while dragging nodes, wires or a box, the view pans within 40 dp of the edge, ramping up to 900 dp/s (xyflow's 15 px per frame).
- **Level of detail:**
  - Below the zoom where 12 dp text would render under about 7 px (roughly 0.55), hide pin labels and inline widgets and keep node titles (ComfyUI).
  - Below 0.15, hide wire labels and flow markers (Rivet).

### Selection and nodes

- **Clicks and drags:**
  - A press becomes a drag after 3 dp of movement, by distance only (Blender; ComfyUI's ADR).
  - The double-click window is 300 ms.
- **Box selection:**
  - A left-drag on empty canvas draws a box that selects nodes it intersects (tldraw, Svelte Flow).
  - Shift adds and Ctrl subtracts (Blender, ComfyUI).
  - A box selection also selects the wires between the selected nodes.
- **Clicking nodes:**
  - A click selects the node.
  - Shift+click toggles it (Blender).
  - Clicking empty canvas clears the selection.
- **Dragging nodes:**
  - Dragging moves the whole selection, and selected nodes are raised.
  - Snapping to the 20 world-unit grid is off by default; Ctrl inverts it while dragging.
  - Snapping aligns the nearest pin to the grid rather than the node corner, so wires stay straight (imgui-node-editor).
  - When several nodes move, the lead node is snapped and the rest move by the same offset (xyflow).
- **Wires while dragging a node:**
  - Alt+drag detaches the node from its wires (Blender).
  - Dragging an unconnected node onto a wire highlights that wire, and dropping inserts the node into it (Blender, Node-RED). Alt skips the insert. Neighbors don't shift aside yet.
- **Keys:**
  - X or Delete deletes; Ctrl+Delete deletes and reconnects matching wires through (Node-RED, ComfyUI).
  - Shift+D duplicates, and Ctrl+C/X/V copy, cut and paste.
  - M mutes (disables), H collapses, F2 renames.
  - Arrow keys walk along wires to the next node in that direction; Shift extends the selection upstream or downstream (n8n).
  - Q straightens the selection (Unreal); L arranges it (Houdini).
- **Undo:** each gesture is one undo step. The editor already snapshots per mutation; camera and selection changes never enter history.

### Node rows

- Rows follow Blender: execution pins pair up on the first rows (Unreal), then every data output, then every data input.
- Inputs therefore always sit below outputs, wires never cross inside a node, and each input row has the full node width.
- Input labels share one width per node, so their editors line up in a column.
- Unreal, Unity and ComfyUI instead set inputs and outputs side by side. That is shorter, but it pairs unrelated pins and leaves editors half a node wide.
- Nodes are measured in C with text measured as the canvas draws it at the current zoom, since hinted text at small pixel sizes runs wider than a scaled measurement. Arrange takes each pin's offset from the same measurement.

Settings under Graph Nodes (all on by default):

- **Short Bindings:** component values read "Aim › Manual" with a component icon, since a component binding always reads the node's owner. Variables, locals, event values and Delta Time get their own icons. Off, chips spell out `Self.GameAim.manual`. Unreal's member getters also omit "self"; its "self" and Unity's "This" appear only as the default of a call's Target pin.
- **Flow in Titles:** an unlabeled execution pin (Exec, Then, In, Out) sits on the title's edge (Unity Visual Scripting). Named branches such as True and False keep their rows, and a node with no other rows is just its title.
- **Compact Nodes:** an operator with a symbol draws it between its stacked inputs and its vertically centered output, without a title or pin labels (Unreal's compact nodes). A node that only reads one value is a pill with its socket on the title, without "Get" (Unreal's variable getters).

Expressions show their source in the node, highlighted and wrapped, under an "Expression" title. Changing a setting arranges automatic nodes again.

### Wires and sockets

- **Pins:**
  - Exec pins are arrows, data pins are circles, and arrays and sets are squares (Blender, Unreal). All are hollow while unconnected and filled once wired (Unreal).
  - Sockets are drawn as antialiased geometry (`kit_dot`), since RmlUi's rounded boxes aren't antialiased and read as squares when small.
  - Colors follow the pin type; data wires take their source's color, and exec wires are white and thicker.
  - The canvas follows the theme. Light canvases keep full-color titles in lighter tints, darken data colors slightly and draw exec wires dark gray (Blender's light theme).
  - Muted wires draw red.
- **Shape:**
  - A cubic bezier leaving along each pin's direction: the handle is `0.5·dx` when the target is ahead of the source.
  - When the target is behind, the handle is `0.25·25·√(−dx)`, so backward wires loop out cleanly (xyflow).
- **Hit testing:**
  - Wires can be hit within 10 dp.
  - Priority is pin, then node, then wire, then empty canvas (imgui-node-editor).
  - Sockets are not picked from inside a node's header.
- **Connecting:**
  - Drag from a socket. The candidate snaps to a socket within 20 dp; a socket directly under the pointer beats the nearest one (xyflow).
  - Validity has three states (xyflow, Rivet):
    - **none:** the wire follows the pointer.
    - **invalid:** the wire turns red and the status bar gives the reason (imgui-node-editor).
    - **valid:** the wire snaps to the socket and shows green. A valid connection that needs a conversion node shows amber.
  - While dragging, incompatible sockets dim to 20% (imgui-node-editor, Unity).
  - Commit happens on release.
- **Where the wire lands:**
  - **On a node's body:** it connects to the first free compatible input. The order is exact type, then wildcard, then occupied exact, then occupied wildcard (ComfyUI).
  - **On empty canvas:** it opens the add search at the drop point, filtered by compatibility, with exact types first. The chosen node spawns with its matching pin under the pointer and wired (Blender, Unreal, ComfyUI).
  - Holding Ctrl on release keeps the wire for fanning out to more inputs (Rivet).
- **Re-editing wires:**
  - Dragging a connected input picks its wire up; releasing it on empty canvas disconnects it (Blender, Rete, Rivet).
  - Ctrl+drag from an output moves all of its wires (Blender, Unreal).
  - Alt+click a pin breaks all its wires (Unreal); Alt+click a wire deletes it (imgui-node-editor).
  - Double-click a wire to add a reroute and drag it (Unreal); drag a reroute to move it and Alt+click it to remove it. A wire on arranged lanes takes the first reroute as its only point. The wire menu clears a wire's reroutes.
  - Ctrl+right-drag cuts the wires it crosses; Shift+right-drag inserts reroutes where it crosses (Blender).

### Values, adding and structure

- **Inline values:**
  - Every unconnected data input shows its editor: number field, checkbox, choice, color or text.
  - Connecting hides the editor but keeps its value, and disconnecting brings it back (Blockly shadow values; our pins already keep `default_value`).
  - Bindings (component fields, variables) show as chips.
  - A folded pure source (see `graph_node_is_inline`) shows as a chip with the expression the input receives, from `behavior_expression_format`, such as `Movement › Speed × 2`. With Short Bindings each value it reads is named like its binding chip, and a chip that is a single read takes that read's icon. The node menu's **Inline** folds an eligible node, and **Expand Inputs** draws a node's folded sources as nodes, which automatic arrangement then places.
  - Per-input policy: connection only, constant only, or either (egui_node_graph).
- **Add menu:**
  - Shift+A, Space or Tab opens the command surface at the pointer, and typing searches immediately (Blender, Houdini).
  - Categories stay browsable, and synonyms match (Shader Graph).
  - It reuses `kit_command` and the editor's existing search catalog.
- **Comments:**
  - C wraps the selection in a comment frame.
  - Dragging a frame carries its contents; Shift leaves them in place.
  - Titles stay readable when zoomed out (Unreal, imgui-node-editor).
- **Functions:**
  - Ctrl+G collapses the selection to a function; the interface is generated from the wires crossing the selection boundary (Blender, Unreal).
  - Tab enters a call and Ctrl+Tab leaves it. Breadcrumbs show the path.
  - These map to `editor_open_graph_call` and `editor_graph_back`.

### Debugging

- **Execution:**
  - Executed exec wires pulse. Markers are spaced 30 units apart and move at 150 units/s for 2 s (imgui-node-editor `Flow`); flow-heat timings stay.
  - The running node gets a 3 dp accent outline, and errors a red one (ComfyUI, Unity).
  - Branches that didn't run draw dashed (Rivet).
- **Breakpoints:** F9 toggles a breakpoint, drawn as a red octagon; outlined means disabled (Unreal).
- **Values:** Watch Values in the node menu watches its data pins. Watched pins show value bubbles beside their sockets, and hovering a pin shows its value the same way (Unreal, Blender). Values come from the inspected run, such as one paused at a breakpoint, or from the preview for pins it can evaluate; previews can't read bindings.
- **Timings:** an optional per-node timing overlay (Blender).

### Automatic layout

Arrange follows ELK Layered's phases, with Godot's MIT arranger as a size reference:

1. **Cycle breaking:** a depth-first search from entry nodes reverses loop back-edges.
2. **Layering:**
   - Exec nodes are placed by longest path along exec wires.
   - A pure data node sits one layer left of its first consumer.
3. **Crossing minimization:**
   - A layer sweep by the median of port positions. Ports keep a fixed order: inputs on the left and outputs on the right, in declaration order.
   - Seeded with the current vertical order, so relayout keeps the user's mental map.
4. **Placement:**
   - Brandes–Köpf, aligning ports to ports.
   - Exec wires are weighted to run straight.
   - Spacing is 80 units between layers and 30 between nodes, snapped to the grid.
5. **Result:** positions animate over 0.25 s as one undo step.

`graph/authoring/arrange.c` is the current implementation to evolve toward this. The equivalent ELK options, for testing:

- `direction=RIGHT`
- `portConstraints=FIXED_ORDER`
- `layering=NETWORK_SIMPLEX`
- `cycleBreaking=DEPTH_FIRST`
- `considerModelOrder=NODES_AND_EDGES`
- `nodePlacement=BRANDES_KOEPF`, with `priority.straightness=10` on exec wires only
- `spacing.nodeNodeBetweenLayers=80` and `spacing.nodeNode=30`

## Canvas architecture

The canvas is a separately scaled RmlUi context, not a transformed element: RmlUi can't clip transformed content, and scaling would blur text. The implementation has four parts:

- **Context and texture.** `rml_backend_resize_pixels` sizes the context to the Behavior area in physical pixels, with a dp ratio of zoom × pixel scale. Every RCSS length in `graph.rcss` is therefore in graph units, and text is rasterized at its displayed size. The texture is shown by a `kit_surface`, and window input is replayed into the context in graph units.
- **Node sizing.** Nodes are sized in C from measured text (`kit_text_width`) and the fixed row pitch arrange assumes. Arrange, framing and hit tests never wait for layout.
- **Wires.** `kit_wires` draws the grid and wires. Its ends anchor to socket elements and resolve after layout, so a wire follows its node in the frame the node moves.
- **Menus and search.** These live in the Studio document, so they stay unscaled.

## Parity with the removed ImGui graph editor

The ImGui graph canvas and panels were removed on September 25, 2026. Studio's graph editor keeps, or must restore, their capabilities:

- Graph calls and caller history.
- Behavior expressions and their inline math.
- Component bindings and the binding popup.
- Inline sources and vector constructors.
- Split and recombined pins, and exposed struct fields.
- Event signatures and payload repair.
- Comment boxes and edge reroutes.
- Breakpoints and pin watches.
- Inspection of a runtime instance: flow heat, traces, the debug target.
- The optimizer, extraction to a function, and auto-arrange.
- Graph undo/redo per document.

Mutations go through the editor's existing helpers and `editor_command_apply`; the view never edits graph memory directly (the xyflow controlled model).
