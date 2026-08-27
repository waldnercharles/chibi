# Phase 3 editor verification

The event-driven preview is available with `./build/app --event-demo --frames
600`. The bounded frame count keeps a verification session reproducible. The
host-authored demo uses the same editor surfaces as a graph built from scratch:
`Begin()`, `Tick(Real DeltaSeconds)`, and `Input(CF_V2 Delta)`.

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
2. Open Content Drawer from Window, select a prototype card, then open it. The
   Prototype workspace shows `Components`, `My Blueprint`, and `Inspector`.
3. Select a component in Components. Inspector shows its reflected default
   fields. `Add Component` and `Remove Component` change the Prototype-owned
   component set and update matching scene instances.
4. Expand My Blueprint. It lists attached Behaviors and their graph members.
   `Add Behavior` and `Detach` update the Prototype and matching instances.
5. Edit a scalar component default in Inspector. Matching instances receive the
   new inherited value immediately. A scene-instance override remains local.
6. Save the active Prototype through File > Save. File > Save As... chooses a
   deterministic path under the content root; File > Reload from disk restores
   the source file.

Prototype structure is Prototype-owned. Scene instances cannot add or remove
components or Behaviors. Input delivery is based on Behavior attachments, not a
designated player entity.

## Preview instance (UI-54, UI-55, UI-57)

1. Open Scene Runtime and use Play, Pause, Step, Restart, and Stop. The target
   starts stopped, and Restart rebuilds from current in-memory authored data.
2. Open a Behavior graph and click Dispatch. The Behavior toolbar shows the
   selected activation state and step count. Compiler Results and Output Log
   remain dockable bottom tabs.
3. While a `Delay` activation is suspended, dispatch another catalog event. The
   instance remains attached while the selected activation changes.
4. Edit a node position, pin value, or connection. Behavior Save writes the
   authored graph and applies it. A blocked apply leaves the previous runtime
   snapshot active and reports the failure in diagnostics.
5. Press and release Space while Scene Runtime has focus. The host sends the
   fixed `Input` signature.

## Authoring surfaces (UI-1, UI-3, UI-4, UI-53)

1. Use My Blueprint to add a variable. Inspector edits its name, type, default,
   container, and category. Duplicate names show a diagnostic.
2. Open the graph spawn menu and create a catalog Event. Its name and pins come
   from the host signature. Inspector shows those pins as read-only. Custom
   Event creation, parameter surgery, and Call Event are unavailable.
3. With no node selected, edit the optional owner handle type and add Self.
4. Select each literal. Its `Value` input is editable on the node and in
   Inspector. Save and load to verify authored values and Name spelling.
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
   dispatches the authored `Begin` entry automatically; observe edge markers
   and heat. Repeat with `--fps 30` and `--fps 144`. The traversal history and
   animation are derived from run records, not evaluator callbacks.

If a previous dock layout has collapsed a panel to a narrow tab, close the
app, remove the ignored `imgui.ini`, and restart before checking the layout rows.
The headless acceptance checks for the runtime, serialization, compile pass,
arrays, delegates, stateful flow, and breakpoint behavior run with:

```sh
cmake --build build && ./build/self_check && ./build/integration
```
