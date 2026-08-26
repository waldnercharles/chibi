# Phase 3 editor verification

The event-driven preview is available with `./build/app --event-demo --frames
600`. The bounded frame count keeps a verification session reproducible. The
host-authored demo uses the same editor surfaces as a graph built from scratch:
`Begin()`, `Tick(Real DeltaSeconds)`, and `Input(Name Action, Real Value,
Bool Pressed)`.

## UI screenshot capture

Use `./build/app --headless --screenshot` to render three settling frames into
the hidden GPU window and save the final composed app canvas. The default path
is `/tmp/c-blueprints-screenshot.png`, and the app prints the path and pixel
size after a successful write. Pass a path after `--screenshot` to choose a
different file, or pass `--frames N` to change the settling period.
The PNG includes the ImGui editor because capture mode renders ImGui into the
app canvas before readback. Pixels are not guaranteed to be bit-for-bit stable
across saved `imgui.ini` state, DPI, GPU backend, or font rasterization.

## Scene runtime MVP

The content root contains an entity prototype with the graph-backed
`InputBehavior` attached. Focus the scene viewport, then press and release
W/A/S/D or the arrow keys. The host broadcasts each edge-triggered `Input`
event to attached behaviors; releases send a zero value and held keys do not
repeat. The headless regression is `./build/input_behavior_check`.

## Prototype behavior attachment

The first Prototype authoring slice follows Lumix's separate asset-browser and
Properties pattern:

1. Reset the layout if needed by removing `imgui.ini`, then start `./build/app`.
2. In Content Drawer, select a prototype card. Selection stays in the Scene
   context and does not open a prototype document or move the central tab.
3. Use the `Prototype Details` panel below the graph `Details` panel on the
   right. It shows the selected prototype identity and its Behaviors.
4. Click `Add Behavior` and choose a behavior asset that is not already
   listed. `InputBehavior` is loaded from `content/behaviors/InputBehavior.json`.
5. The attachment updates the prototype graph and currently spawned actors that
   use that prototype. The panel reports success or the failure reason.
6. Use the Content Drawer prototype action `Spawn instance` to check that later
   instances inherit the prototype behavior list.

The command is currently in memory. Prototype Details does not yet remove
attachments, edit Components, save a prototype, or run the full live-apply
lifecycle. Input delivery is based on behavior attachments, not a designated
player entity.

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
   they pass rather than through them. The graph view remains the source of
   truth for navigation and layout.
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
