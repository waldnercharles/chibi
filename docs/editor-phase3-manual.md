# Phase 3 editor manual verification (pending)

What the Phase 3 editor commit (`editor/` + `app/`) needs a human to click
through. None of this is recorded in `manual-verification.md` yet; add rows
there with the verifying commit after each item has actually been exercised.

## Preview instance (UI-54, UI-55, UI-56)

1. With the flow demo graph open, click **Dispatch**. Dispatch attaches the
   runtime automatically. The toolbar reads `Runtime: attached`; the selected
   event runs, `State` and `Steps` update, and the output log receives `Print
   String` output.
2. While an activation is suspended (Delay), dispatch another event. The two
   activations coexist on one instance: instance variables written by the
   first remain visible to the second (UI-54 distinguishes the instance from
   the selected activation).
3. Click **Stop** on a running activation: instance variables and scratch
   survive for the next dispatch (RUN-17).
4. Make any authoring edit while attached: drag a node, add a pin, edit a
   value, or undo. The toolbar states `Runtime reset by the last edit`, the
   edit itself succeeds (it is never presented as a rejected edit), and the
   next **Dispatch** recreates a fresh instance with variables re-initialized
   from the new authored defaults (UI-55, UI-56, VAR-11).
5. In the app, press Space: an authored `Input` event fires if present; edits
   that destroy the instance reset the demo, and `Begin()` fires again when the
   host recreates the runtime.

## Variables (UI-1, UI-3, UI-4, VAR-1, VAR-3, VAR-5)

1. `+ New Variable` in the Blueprint tree creates `NewVariableN`; the details
   panel renames it, retypes it (including to Name or Handle), and edits its
   default. A rename to an existing name is rejected with a toast (VAR-3).
2. Retyping a variable updates the display and pin type of existing Get/Set
   nodes (VAR-5). Delete leaves orphaned Get/Set references behind (VAR-7).
3. **Add Get node** and **Add Set node** drop typed nodes on the canvas; wire
   a Get into a literal-less input and confirm preview shows the variable's
   current instance value, not the authored default.
4. Save with Ctrl+S, edit the JSON, load with Ctrl+O: name, description,
   owner type, and variables round-trip (FMT-16, FMT-17).

## Event signatures (EVT-2, EVT-4, EVT-6)

 1. On an Event node: rename it, add a parameter, rename/retype/reorder/remove
    parameters, and set parameter defaults in the details panel.
 2. **Add Call Event node** creates a Call Event whose inputs mirror the
    event's parameters; wire and dispatch it.

## Owner type and Self (BP-12, UI-53, CAT-32)

 1. In the blueprint details (nothing selected), set an owner handle type,
    then **Add Self node**: the node's output is a handle pin carrying that
    nominal type name. Clearing the field removes the owner type.

## Literal values (CAT-30, UI-1)

 1. Every literal (Bool, Byte, Integer, Integer64, Real, Name, String) has one
    editable Value input on the canvas and in the details panel; typed values
    round-trip through save/load. Name and String accept text.

## Known gaps (not part of this commit)

- VAR-10 (promote a pin to a variable) is not implemented; it stays a TODO.
- A variable's container and category are not authorable: core exposes no
  setter (`blueprint_retype_variable` covers type only), so UI-1 for those two
  fields is deferred until a core API exists.
- The Blueprint name itself is not authorable (no core setter); description
  and owner type are.
