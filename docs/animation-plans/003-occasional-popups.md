# 003 — Fade occasional pointer popups

- **Status**: Implemented, pending coordinated build and native verification
- **Commit**: 90fc6e6f
- **Severity**: LOW
- **Category**: Missed opportunity; native physicality
- **Estimated scope**: 2 files plus narrow helper declaration, about60 lines

## Problem

editor/ui_shell.c:758 opens Manage at full alpha:

```c
if (!ImGui_BeginPopup("attachments", 0)) {
    ImGui_PopID();
    return false;
}
```

engine/viewport.c:343 similarly uses:

```c
if (ImGui_BeginPopup("viewport_more", 0)) {
    *hovered = true;
```

These occasional menus are eligible for a short appearance transition. The command palette, node picker, search rows and keyboard menus are not.

## Target

For pointer-opened Manage and compact Scene More only, alpha enters from 0 to 1 over 125ms using cubic-bezier(0.23,1,0.32,1). Popup position, size, clip geometry and hit testing remain at final values from the first frame. Keyboard opening is fully opaque immediately. Dismissal is immediate. Reduced Motion retains only this opacity feedback and no spatial movement. Do not scale text or draw vertices independently from hit geometry.

The no-scale choice is a native adaptation: ImGui does not supply a CSS-style transform that updates rendering and hit testing together. This plan uses stable native geometry, not a partial transform with mismatched input.

## Repo conventions to follow

Use small file-private C helpers and ImGuiStorage keyed below the control ID. Existing ui_icon_button in editor/ui_shell.c:856 demonstrates per-item storage and fixed hit bounds; ui_disclosure at :1011 demonstrates retargeting from the currently drawn state. Use editor_ui_reduced_motion(). Keep active color and keyboard focus visible. No dependency additions.

## Steps

1. Add a narrow ui_shell helper for these opt-in popup entries. Store opening input origin and timestamp under the popup's owner ID when its trigger opens it. Do not derive origin from global mouse state on subsequent frames.
2. Before BeginPopup, compute opacity for the current 125ms entry and push ImGuiStyleVar_Alpha multiplied by the parent alpha. Keep the alpha style scoped through popup content and EndPopup, with a balanced pop on every success/failure path. This includes popup background, which is emitted during BeginPopup; pushing alpha after BeginPopup would miss it.
3. Reopening initializes a new entry only after the prior popup has closed. Existing open popup calls must not restart its timer. If keyboard navigation begins during a pointer fade, snap opaque.
4. Opt in only Manage attachments and viewport_more. Maintain viewport input blocking from the first popup frame, focus initialization and Escape closing. Coordinate minimal scene_imgui declarations if required by the viewport's C ABI subset.
5. Verify a first click on a menu item during entry works immediately. Do not retain an invisible popup for an exit transition. No new render textures or shaders.

## Boundaries

Do not alter document selection, Undo, runtime state, node data, popup contents or command dispatch. Coordinate shared source ownership and builds with the root agent. Line numbers are the inspected baseline; if the named function or quoted implementation has changed materially, report the drift to the root owner before editing that function. Do not revert concurrent edits or format unrelated files.

## Verification

Mechanical: after the coordinated build slot is granted, run `make test`; expect existing keyboard, viewport input and native UI tests to pass. Add focused assertions only for state transitions introduced by this plan. Tests must cover actual input origin, not merely compare an implementation formula to itself.

Feel check: run the native app at normal timing and temporarily use a development-only 5× duration multiplier or frame stepping. Exercise the named controls, then remove that temporary multiplier. Toggle the existing Reduced Motion preference and confirm that movement disappears while color/focus feedback remains. Inspect at 1× and 2× DPI. Do not substitute browser DevTools instructions for this native workflow.

Done when both eligible menus fade only on pointer opening, remain anchored to the existing ImGui trigger placement at narrow viewport edges, and keyboard opening/closing stays immediate. Confirm rapid open/Escape/reopen never inherits a stale low opacity or restarts while already open.
