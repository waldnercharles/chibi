# Phase 10 editor-only scene authoring

This is the window procedure for the remaining UI-1 gate. Run it from a clean
working tree after building:

```sh
cmake --build build
./build/app --frames 0 --no-seed
```

1. In the Blueprint panel, use the spawn menu to add an Event node. Select the
   node and set its event name to `Tick` in the details panel.
2. Add a flow node and connect the Event exec output to it on the canvas. Set
   any exposed scalar value in the node editor, not in a file.
3. In Scene authoring, click `+ Player` and `+ Enemy`. Select each entity and
   edit its Player flag, Tick graph attachment, transform, and health fields.
4. Click `Compile / Apply graph`. The status line must report the applied
   definition without destroying the other entity's components.
5. Focus Scene, press and release Space, then focus Blueprint and move a node.
   The input and graph edit must remain available through their respective
   panels.
6. Close the app without saving. Confirm that no JSON or prototype source was
   edited. Record the exact commit in `docs/manual-verification.md` only after
   completing these steps.

The scene panel owns the ECS entity/component authoring controls. The Blueprint
panel owns graph authoring. `Compile / Apply graph` is the only runtime snapshot
boundary; no prototype or JSON file is required for the check.
