# Node editor references

Studio source and shell assets are owned by the extracted `chibi-studio`
repository. Paths under `src/editor/` and `projects/studio/` in this guide are
relative to that repository. The engine supplies the shared SDK interfaces.

This document is the node-graph counterpart of [Editor UI references](editor-ui-references.md). It covers the libraries and editors whose interaction contracts the RmlUi behavior graph editor ports. We port behavior, not code. Numbers were read from source or official docs on 2026-09-23; **(unverified)** marks the few that weren't.

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
  - Continuous and anchored at the cursor: `zoom · 2^(0.2 · notches)`, about ×1.15 a mouse notch, and a browser's 100 px of trackpad scroll is a notch, so `2^(−Δy · 0.002)` per pixel (xyflow). Every zoomable canvas shares the rate (`src/ui/input_feel.h`).
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
  - A press becomes a drag after 4 dp of movement, by distance only (Blender's 3 px; ComfyUI's ADR), as everywhere in Studio.
  - The double-click window is 500 ms and 3 dp, the operating systems' and RmlUi's, as everywhere in Studio.
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
  - Snapping to a 16 world-unit grid, half a drawn cell, is off by default; holding Ctrl while dragging snaps.
  - Snapping aligns the nearest pin to the grid rather than the node corner, so wires stay straight (imgui-node-editor).
  - When several nodes move, the lead node is snapped and the rest move by the same offset (xyflow).
- **Wires while dragging a node:**
  - Alt+drag detaches the node from its wires (Blender). Not implemented yet.
  - Dragging an unconnected node onto a wire highlights that wire, and dropping inserts the node into it (Blender, Node-RED). Alt skips the insert. Neighbors don't shift aside yet.
- **Keys:**
  - X or Delete deletes; Ctrl+Delete deletes and reconnects matching wires through (Node-RED, ComfyUI). Ctrl+Delete is not implemented yet.
  - Shift+D duplicates, and Ctrl+C/X/V copy, cut and paste. Pasted nodes center on the pointer (Unreal).
  - M mutes (disables), H collapses, F2 renames. H is not implemented yet.
  - Arrow keys walk along wires to the next node in that direction; Shift extends the selection upstream or downstream (n8n).
  - Q straightens the selection (Unreal): left to right, each node moves level with the wire that drives it, or with its first output wire when nothing drives it. A selected wire, or Straighten Connection in the wire menu, moves its target. Shift+L arranges the selection (Houdini).
  - L adds the nodes wired to the selection's inputs and Alt+L those wired to its outputs, one step at a time (Blender's Select Linked From and To; Blender's Shift+L is Arrange here). The node menu's Select submenu also follows the wires to the end (Unreal's Select All Input and Output Nodes). Nodes folded into their consumer are passed through.
- **Alignment:** with two or more nodes selected, the node menu's Alignment submenu aligns their tops, middles, bottoms, lefts, centers or rights to the selection's bounds, and distributes three or more with equal gaps (Unreal, Figma). Each item shows its alignment icon. The graph menu offers the same for the selection.
- **Undo:** each gesture is one undo step. The editor already snapshots per mutation; camera and selection changes never enter history.

### Node rows

- Rows follow Blender: execution pins pair up on the first rows (Unreal), then every data output, then every data input.
- Inputs therefore always sit below outputs, wires never cross inside a node, and each input row has the full node width.
- Input labels share one width per node, so their editors line up in a column.
- Unreal, Unity and ComfyUI instead set inputs and outputs side by side. That is shorter, but it pairs unrelated pins and leaves editors half a node wide.
- Nodes are measured in C with zoom-neutral text metrics, so node bounds and socket positions stay fixed in graph units while the camera moves. The canvas still rasterizes text at its displayed size. Arrange takes each pin's offset from the same measurement.

Settings under Graph Nodes (all on by default):

- **Short Bindings:** component values read "Aim › Manual" with a component icon, since a component binding always reads the node's owner. Variables, locals, event values and Delta Time get their own icons. Off, chips spell out `Self.Aim.manual`. Unreal's member getters also omit "self"; its "self" and Unity's "This" appear only as the default of a call's Target pin.
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
    - **valid:** the wire snaps to the socket and shows green. A valid connection that needs a conversion node shows dashed, like the converting wire it becomes.
  - While dragging, incompatible sockets dim to 20% (imgui-node-editor, Unity).
  - Commit happens on release.
- **Where the wire lands:**
  - **On a node's body:** it connects to the first free compatible input. The order is exact type, then wildcard, then occupied exact, then occupied wildcard (ComfyUI).
  - **On empty canvas:** it opens the add search at the drop point, filtered by compatibility, with exact types first. The chosen node spawns with its matching pin under the pointer and wired (Blender, Unreal, ComfyUI).
  - Holding Ctrl on release keeps the wire for fanning out to more inputs (Rivet). Not implemented yet.
- **Re-editing wires:**
  - Dragging a connected input picks its wire up; releasing it on empty canvas disconnects it (Blender, Rete, Rivet).
  - Ctrl+drag from an output moves all of its wires (Blender, Unreal).
  - Alt+click a pin breaks all its wires (Unreal); Alt+click a wire deletes it (imgui-node-editor).
  - Right-clicking a pin opens its menu (Unreal): Break Link, or Break All Links and Break Link To one wire; Reset to Default, which also clears a binding; Promote to Variable, which adds a Get beside an input or a Set beside an output (the Set runs after the node when the node's only execution output is free, as Unreal wires it to Then, and otherwise sits below the node, unwired); Name Value or Remove Name on an output; Split or Recombine Struct Pin; and Remove Pin.
  - Double-click a wire to add a reroute and drag it (Unreal); drag a reroute to move it and Alt+click it to remove it. A wire on arranged lanes takes the first reroute as its only point. The wire menu clears a wire's reroutes.
  - Ctrl+right-drag cuts the wires it crosses; Shift+right-drag inserts reroutes where it crosses (Blender). Neither is implemented yet.

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
  - Holding a key and clicking empty canvas places a node there (Unreal): B Branch, S Sequence, D Delay, F For Each, N Do N, O Do Once, G Gate. F alone frames once released; M stays free to mute.
- **Variadic nodes:** Sequence, MultiGate, the Int, String and Name switches, Select, Make Array, Make Set and Format Text grow a run of like pins. The node menu's Add Pin and a last Add Pin row add one (Unreal); Format Text keeps a spare argument instead of the row. The pin menu removes one while another remains. A switch's Default stays last, a new Select or Make Array pin joins its wildcard group with the resolved type, and generated names renumber.
- **Struct pins:** Split Struct Pin shows a struct output's fields as outputs beside it, which any node can evaluate; Recombine hides the unwired ones. On Set Component, Value splits into the settable field inputs.
- **Comments:**
  - C wraps the selection in a comment frame.
  - Dragging a frame carries its contents; Shift leaves them in place.
  - Double-clicking a frame's title renames it in place (Unreal); its edges and corners resize it (Blender, Unreal), and its menu sets a color from a palette.
  - Titles stay readable when zoomed out (Unreal, imgui-node-editor).
  - Clicking a frame selects it; the Node tab then edits its title and color, and Delete removes it (Unreal's comment Details). The model has no font size, so none is offered.
  - A node can carry a comment, drawn as a bubble above it (Unreal's node comments). It is edited from the node menu, a double-click on the bubble or the Node tab. Comments span lines: Shift+Enter starts a line and Enter commits. A hidden comment shows only while the node is hovered, and titles-only zoom hides bubbles.
- **Find:**
  - Ctrl+F (⌘F) opens a field over the canvas that lists the behavior's nodes whose title, pin names, values, bindings, expression source or comment contain the query, graph by graph (Unreal's Find Results, Blender's Find Node).
  - Up and Down step through the results in the graph; Enter or a click jumps to one, selecting and framing it.
  - Find References (Shift+Alt+F, the node, graph and asset menus) lists every node across the project that calls, raises, handles, spawns, reads or writes a function, variable, event or asset, in Output's Find view; a row opens its asset and reveals the node (Unreal).
- **Functions:**
  - Ctrl+G collapses the selection to a function; the interface is generated from the wires crossing the selection boundary (Blender, Unreal).
  - Collapse to Macro makes a macro instead. Every execution wire crossing the selection becomes a tunnel pin named after the pin it crosses at, so a Branch keeps its True and False exits and latent nodes such as Delay can move inside (Unreal).
  - Expand Node puts the body of a call to one of the behavior's own functions or macros back in the call's place, wired as the call was; the called graph stays (Unreal). A function expands only when every execution output in its body is wired, since its call continues wherever the body ends.
  - Refresh Nodes updates the selection's library calls to their functions' current signatures (Unreal). Local calls follow signature edits as they happen.
  - Unreal's Collapse Nodes has no counterpart: every graph is a named function or macro of the behavior, and no graph belongs to a single node.
  - Tab enters a call and Ctrl+Tab leaves it. Breadcrumbs show the behavior and the calls entered; each returns to its graph (Unreal).
  - These map to `editor_open_graph_call`, `editor_graph_back` and `editor_graph_ancestor`.
  - The Member tab edits a function's or macro's inputs and outputs: name, type, container (Single, Array, Set or Map) and removal, and each data input's default. A macro's pins may also be Exec (Unreal's tunnels). Calls follow every edit (`graph/document/graph_signature.h`); a call pin still showing the old default takes the new one, and a value set on the call stays (Unreal).
  - Functions of materials, shapes, shader libraries and fields are pure.
  - A behavior has one event graph. Unreal's Add New Graph pages share one Ubergraph, but here only the default graph's events are dispatched, compiled for duplicates and batched, so extra event graphs would never run.
- **History:** Back and Forward (Alt+Left and Alt+Right, or mouse buttons 4 and 5) move through the graphs visited, across behaviors (Unreal).

### Debugging

- **Debug target:** while the game runs, the graph shows one instance, which follows the selection; the header's target menu lists every running instance, searchable, and No target (Unreal's debug object). Only the target records activity: a count and the last time per wire (`behavior_instance_record_activity`), and its latest completed run per event for values. Where compiled code carries no observers, the target runs it interpreted, so every wire and value is recorded; other instances keep their compiled code.
- **Execution:**
  - Wires the target ran pulse (Unreal): bubbles 30 screen dp apart travel at 120 screen dp/s, whole while the wire fires again within 0.1 s, then shrink to nothing over 0.4 s. A data wire pulses when its consumer pulls its value. Execution wires also keep the flow-heat glow for about 2.6 s. Reduce Motion shows only the glow.
  - A node the target is running wears a rim that fades over 0.4 s; its title's tooltip gives its run count and last run.
  - The running node gets a 3 dp accent outline, and errors a red one (ComfyUI, Unity).
  - Branches that didn't run draw dashed (Rivet).
- **Breakpoints:** F9 toggles a breakpoint, drawn as a red octagon; outlined means disabled (Unreal).
- **Stepping:**
  - A run paused at a breakpoint pauses the scene, opens its node's graph, rings the node and makes its instance the debug target (Unreal).
  - The transport trades Pause for Resume (F5) and the frame step for Step Over (F10), Step Into (F11) and Step Out (Shift F11), Visual Studio's keys, which Unreal follows. Space also resumes.
  - Step Over runs a call as one node, Step Into stops at the call's first node, and Step Out finishes the call and stops in its caller.
  - Each step pans the node into view unless it is well inside it (VS Code).
  - The call stack floats over the canvas's corner, innermost call first; a row opens its node.
- **Problems:**
  - The open behavior compiles whenever it changes, so no Compile button is needed.
  - A node with an error or warning gets a badge on its top edge whose tooltip gives the message (Blender's node warnings).
  - While the scene plays, a node where runs failed gets an error badge.
  - Output ▸ Problems lists diagnostics as "Graph › Node" and the message (Unreal's Compiler Results). Output ▸ Runtime lists failed runs as "Behavior › Node", one row per node and message with a count (Unreal's PIE message log). A row opens the node's graph, selects the node and frames it.
- **Values:** Watch Values in the node menu watches its data pins. Watched pins show value bubbles beside their sockets, and hovering a pin shows its value the same way (Unreal, Blender). Show Live Values gives every data output a quieter chip (Unreal's watches everywhere), refreshed ten times a second. Values come from the inspected run, such as one paused at a breakpoint, else from the target's latest completed runs; a pure output reads as what its consumer received. Without a running target they come from the preview for pins it can evaluate; previews can't read bindings.
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
