# Editor feature parity plan

Superseded 2026-08-17 by docs/port-plan.md: the editor is now a full port of
imgui-node-editor core to C23. The feature list below still applies to the
"extras" phase (undo/redo, clipboard, mini-map, context menus, search).

A straight imgui-node-editor -> C23 port means reimplementing roughly 5k lines
of std::vector/map/string/lambda/template code for no gain to the blueprint
engine. Section 14 already scopes this: reimplement each feature; port one
algorithm only when reimplementation cannot reproduce it.

## Now (dependency order)

1. [done] Layout persistence: node x/y on `struct node`, written and read by the JSON
   serializer. Unblocks undo and clipboard, which snapshot the graph.
2. [done] Multi-select: marquee rectangle, shift/ctrl-click, group drag.
3. [done] Undo/redo: full-graph JSON snapshot stack (reuses the serializer).
4. Cut/copy/paste: subgraph serialize + append-with-offset.

## Later

5. Context menus (node/link/background) + search filter in the create popup.
6. Hover highlights (node/pin/link) + link deletion.
7. Mini-map + UE4-blueprint theme polish.

Each step lands with a self-check where the logic is testable at the core
level; interaction-only changes rely on the existing build plus manual smoke.
