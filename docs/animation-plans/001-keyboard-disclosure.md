# 001 — Make keyboard disclosure immediate

- **Status**: Implemented, pending coordinated build and native verification
- **Commit**: 90fc6e6f
- **Severity**: MEDIUM
- **Category**: Purpose/frequency; accessibility
- **Estimated scope**: 1 file, about20 lines

## Problem

Keyboard Left/Right in editor/ui_shell.c:1024 changes `open`, but then unconditionally enters the same animated state as mouse activation at :1040:

```c
float target = open ? 1.0f : 0.0f;
float progress = ImGuiStorage_GetFloat(storage, animation_id, target);
float blend = ui_reduce_motion ? 1.0f
                               : 1.0f - expf(-ImGui_GetIO()->DeltaTime / 0.04f);
progress += (target - progress) * blend;
```

This moves the chevron after a keyboard state change, even though the component contents update immediately.

## Target

Keyboard Enter/Space and Left/Right set the displayed progress to the target in the same frame. Pointer toggles retain the current 40ms exponential time constant and can reverse mid-motion without restarting. Reduced Motion always sets the target immediately. No body-height animation.

## Repo conventions to follow

Use small file-private C helpers and ImGuiStorage keyed below the control ID. Existing ui_icon_button in editor/ui_shell.c:856 demonstrates per-item storage and fixed hit bounds; ui_disclosure at :1011 demonstrates retargeting from the currently drawn state. Use editor_ui_reduced_motion(). Keep active color and keyboard focus visible. No dependency additions.

## Steps

1. In ui_disclosure, capture activation origin when InvisibleButton reports activation. Use ImGui context navigation activation/source information supported by this version, rather than global MouseDown alone; keyboard use can coexist with a resting mouse. Explicit Left/Right branches mark the change as keyboard.
2. When a keyboard state change occurs, set progress=target before storing it. Do not smooth a later frame back toward the previous value. A keyboard change during an existing pointer transition cancels the transition.
3. Preserve mouse toggles, focus ownership, tooltip behavior and current rotation geometry. Keep contents immediate.
4. Add focused keyboard test coverage for Left/Right, Space and a keyboard change during an unfinished pointer toggle. Reuse existing native keyboard infrastructure; root owns its registration.

## Boundaries

Do not alter document selection, Undo, runtime state, node data, popup contents or command dispatch. Coordinate shared source ownership and builds with the root agent. Line numbers are the inspected baseline; if the named function or quoted implementation has changed materially, report the drift to the root owner before editing that function. Do not revert concurrent edits or format unrelated files.

## Verification

Mechanical: after the coordinated build slot is granted, run `make test`; expect existing keyboard, viewport input and native UI tests to pass. Add focused assertions only for state transitions introduced by this plan. Tests must cover actual input origin, not merely compare an implementation formula to itself.

Feel check: run the native app at normal timing and temporarily use a development-only 5× duration multiplier or frame stepping. Exercise the named controls, then remove that temporary multiplier. Toggle the existing Reduced Motion preference and confirm that movement disappears while color/focus feedback remains. Inspect at 1× and 2× DPI. Do not substitute browser DevTools instructions for this native workflow.

Done when keyboard progress reaches exactly0 or1 in the input frame, pointer reversal continues from the current angle, and no focus or toggle behavior regresses.
