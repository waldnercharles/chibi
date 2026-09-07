# 004 — Shorten connection error fade

- **Status**: Implemented, pending coordinated build and native verification
- **Commit**: 90fc6e6f
- **Severity**: LOW
- **Category**: Timing
- **Estimated scope**: 1 file,1 constant

## Problem

editor/editor.c:54–55 contains:

```c
#define ED_TOAST_DURATION 4.0
#define ED_TOAST_FADE 0.4
```

ed_connect_error_alpha at :980 uses ED_TOAST_FADE for linear opacity after the readable dwell. Its400ms fade exceeds the review's 300ms UI transition budget.

## Target

Keep ED_TOAST_DURATION4.0 seconds and all error semantics unchanged. Set ED_TOAST_FADE0.2 seconds. Keep existing linear opacity and stable position. This is a small quiet exit, not a new movement or physical transition; no Bézier helper is necessary. Reduced Motion may retain opacity feedback.

## Repo conventions to follow

Use small file-private C helpers and ImGuiStorage keyed below the control ID. Existing ui_icon_button in editor/ui_shell.c:856 demonstrates per-item storage and fixed hit bounds; ui_disclosure at :1011 demonstrates retargeting from the currently drawn state. Use editor_ui_reduced_motion(). Keep active color and keyboard focus visible. No dependency additions.

## Steps

1. Change only ED_TOAST_FADE from 0.4 to0.2 in editor/editor.c. Avoid touching concurrent graph/node work.
2. Check that error replacement still resets the existing deadline, dismissal remains immediate, and toast hit bounds expire with its existing visibility calculation.

## Boundaries

Do not alter document selection, Undo, runtime state, node data, popup contents or command dispatch. Coordinate shared source ownership and builds with the root agent. Line numbers are the inspected baseline; if the named function or quoted implementation has changed materially, report the drift to the root owner before editing that function. Do not revert concurrent edits or format unrelated files.

## Verification

Mechanical: after the coordinated build slot is granted, run `make test`; expect existing keyboard, viewport input and native UI tests to pass. Add focused assertions only for state transitions introduced by this plan. Tests must cover actual input origin, not merely compare an implementation formula to itself.

Feel check: run the native app at normal timing and temporarily use a development-only 5× duration multiplier or frame stepping. Exercise the named controls, then remove that temporary multiplier. Toggle the existing Reduced Motion preference and confirm that movement disappears while color/focus feedback remains. Inspect at 1× and 2× DPI. Do not substitute browser DevTools instructions for this native workflow.

Done when a connection error remains fully readable for 4s and disappears by 4.2s; dismissal and replacing the error remain immediate. No new test is needed solely to mirror the constant. Existing integration coverage plus a native timing check is sufficient.
