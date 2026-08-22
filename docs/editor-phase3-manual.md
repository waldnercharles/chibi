# Phase 3 editor verification

The event-driven preview is available with `./build/app --event-demo --frames
600`. The bounded frame count keeps a verification session reproducible. The
host-authored demo uses the same editor surfaces as a graph built from scratch:
`Begin()`, `Tick(Real DeltaSeconds)`, and `Input(Name Action, Real Value,
Bool Pressed)`.

## Preview instance (UI-54, UI-55, UI-56)

1. Start the event demo and click **Dispatch**. The toolbar shows `Runtime:
   attached`, the selected activation state and step count, and the output log
   receives `Print String` output.
2. While a `Delay` activation is suspended, dispatch another event. The
   instance remains attached while the selected activation changes.
3. Click **Stop**. The next dispatch sees the same instance variables and node
   scratch. Destroying the preview instance clears both.
4. Edit a node position, pin value, or connection while attached. The edit is
   accepted after the preview is destroyed; the toolbar reports `Runtime reset
   by the last edit`. Dispatch creates a fresh instance and fires `Begin()`.
5. Press and release Space. The host sends `Input` with the authored Name,
   Real, and Bool parameters.

## Authoring surfaces (UI-1, UI-3, UI-4, UI-53)

1. Use the Blueprint tree to add a variable. The details panel edits its name,
   type, default, container, and category. Duplicate names show a diagnostic.
2. Select an Event node. Edit its name and parameter pins in the details panel,
   including name, type, order, and default. Add a Call Event node and dispatch
   it.
3. With no node selected, edit the optional owner handle type and add Self.
4. Select each literal. Its `Value` input is editable on the node and in the
   details panel. Save and load to verify authored values and Name spelling.
5. Click **Compile**. Compiler Results lists diagnostics; selecting one reveals
   the named node.

## Debugger and layout

1. Set a breakpoint from a node context menu. The Breakpoints panel lists it
   and provides a global enable toggle.
2. Use **Zoom to fit** and **Auto arrange** on a graph. Auto arrange should
   leave the graph in left-to-right columns with no overlapping nodes, wires
   between connected pins drawn flat, and long wires routed around the nodes
   they pass rather than through them. The Minimap panel reports node and link
   counts and persists with the ImGui dock layout.
3. Run the flow demo with `./build/app --flow-demo --frames 600 --fps 60`. It
   dispatches the authored `OnStart` entry automatically; observe edge markers
   and heat. Repeat with `--fps 30` and `--fps 144`. The traversal history and
   animation are derived from run records, not evaluator callbacks.

If a previous dock layout has collapsed a panel to a narrow tab, close the
app, remove the ignored `imgui.ini`, and restart before checking the layout rows.
The headless acceptance checks for the runtime, serialization, compile pass,
arrays, delegates, stateful flow, and breakpoint behavior run with:

```sh
cmake --build build && ./build/self_check && ./build/integration
```
