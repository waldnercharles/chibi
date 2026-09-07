# 002 — Shorten hover and add restrained pointer press

- **Status**: Implemented, pending coordinated build and native verification
- **Commit**: 90fc6e6f
- **Severity**: LOW
- **Category**: Timing; feedback; cohesion
- **Estimated scope**: 1 file, about50 lines

## Problem

editor/ui_shell.c:875 currently uses:

```c
float blend = ui_reduce_motion
                ? 1.0f
                : 1.0f - expf(-ImGui_GetIO()->DeltaTime / 0.075f);
```

A time constant is not a duration:75ms gives roughly225ms to 95%. Active state at :885 is already clear:

```c
if (enabled && ImGui_IsItemActive()) {
    background = UI_BUTTON_ACTIVE;
}
```

engine/viewport.c:300 and editor/editor.c:5998 already use this shared button, so they inherit its rendering without new per-canvas logic.

## Target

Use named UI_HOVER_TIME_CONSTANT=0.04f, approximately120ms to 95%, for mouse hover color. Keep enabled/disabled and active colors immediate. Add optional mouse press compression of only the drawn backplate/icon from1.0 to 0.97 over 100ms with cubic-bezier(0.23,1,0.32,1). Release returns to 1.0 immediately. The deliberate press is slower than the response. Keyboard activation and Reduced Motion use scale 1.0. Keep the InvisibleButton hit box and navigation focus rectangle unchanged.

Store current scale, starting scale, target and elapsed time in the item's ImGuiStorage. On a changed target retarget from current scale. Clamp elapsed to 100ms and use exact endpoints. Evaluate cubic Bézier y after solving x using a small bounded helper; do not confuse parameter t with elapsed progress. Existing color interpolation remains exponential and is not renamed ease-out.

## Repo conventions to follow

Use small file-private C helpers and ImGuiStorage keyed below the control ID. Existing ui_icon_button in editor/ui_shell.c:856 demonstrates per-item storage and fixed hit bounds; ui_disclosure at :1011 demonstrates retargeting from the currently drawn state. Use editor_ui_reduced_motion(). Keep active color and keyboard focus visible. No dependency additions.

## Steps

1. Replace the hover literal with the file-private named40ms time constant. Disabled hover resets immediately so a stale hover cannot brighten a disabled control.
2. In ui_icon_button detect pointer activation using the active input source. Preserve immediate active color for both keyboard and pointer.
3. Add the bounded per-item press state. Reduced Motion or keyboard activation snaps scale to 1.0 and clears any pointer press state.
4. Derive visual min/max around the button center using scale. Draw backplate, hovered border and glyph within those visual bounds; multiply glyph font size by scale. Leave original InvisibleButton rectangle, layout, tracked rectangle and ImGui_RenderNavCursor unchanged.
5. Confirm tooltip/hover regions do not shrink and action still fires on its original frame. Do not animate Play-to-Pause glyph replacement.

## Boundaries

Do not alter document selection, Undo, runtime state, node data, popup contents or command dispatch. Coordinate shared source ownership and builds with the root agent. Line numbers are the inspected baseline; if the named function or quoted implementation has changed materially, report the drift to the root owner before editing that function. Do not revert concurrent edits or format unrelated files.

## Verification

Mechanical: after the coordinated build slot is granted, run `make test`; expect existing keyboard, viewport input and native UI tests to pass. Add focused assertions only for state transitions introduced by this plan. Tests must cover actual input origin, not merely compare an implementation formula to itself.

Feel check: run the native app at normal timing and temporarily use a development-only 5× duration multiplier or frame stepping. Exercise the named controls, then remove that temporary multiplier. Toggle the existing Reduced Motion preference and confirm that movement disappears while color/focus feedback remains. Inspect at 1× and 2× DPI. Do not substitute browser DevTools instructions for this native workflow.

Done when a held pointer press settles at0.97 within 100ms, release and keyboard activation have no delayed response, and the outer edge of the original button remains clickable throughout the press. Native visual inspection may reject compression if 16px glyph rasterization shimmers; retain the hover shortening and active color in that case and record the measured reason.
