# imgui-node-editor C23 port plan

Decision (2026-08-17): full port of imgui-node-editor core to C23, reversing
design.md decision 1's "own it, don't port" stance. Container lib: ckit.h
(vendored in third_party/ckit/). Keep the graph/serialize/eval data layer.

Measured upstream scope (thedmd/imgui-node-editor master): ~10k lines. std
usage is thin — 12 `std::vector`, 19 `std::string`, 8 `std::sort`, 3
`std::find_if`, 1 `std::map` (bezier cache), ~19 lambdas, 2 trivial templates
in the .cpp. No `std::unordered_map`/`std::function`/`std::shared_ptr`.

## File map (upstream -> ours)

| upstream                                                  | target (editor/ne/)       | notes                                 |
| --------------------------------------------------------- | ------------------------- | ------------------------------------- |
| imgui_extra_math.h/.inl                                   | ne_math.h                 | ImVec2 operators                      |
| imgui_bezier_math.h/.inl                                  | ne_math.h                 | ImVec2-only; drop ImVec3/T genericity |
| imgui_canvas.h/.cpp                                       | ne_canvas.h / ne_canvas.c | 2D canvas + navigation                |
| imgui_node_editor.h                                       | ne.h                      | public API, `ed::` -> `ne_`           |
| imgui_node_editor_internal.h/.inl + imgui_node_editor.cpp | ne_internal.h + ne.c      | editor core                           |

Skipped: crude_json (keep yyjson), imconfig, the C++ example apps.

## Translation rules

- `std::vector<T>` -> ckit dynamic array: `T* v = NULL;` + `apush`/`apop`/`afree`/`acount`/`acap`.
- `std::string` -> ckit `sintern` for immutable strings, or a small string-builder where content is built up.
- `std::map`/id->index lookups -> ckit `Map` (keys are node/pin/link ids, already u64).
- lambda -> `static` function + context struct, or inline the body.
- `template<typename T>` math -> ImVec2-specialized function (drop ImVec3 and generic T).
- `std::sort`/`std::find_if` -> `qsort` / a manual loop.
- `ed::Foo` -> `ne_Foo` / `ne_foo`.
- `ImGui::*` (C++) -> `ImGui_*` (dcimgui C), the repo's existing convention.

## Build order and lanes

1. `ne_math` (extra + bezier) — done, wired into `libne` (compiles + links).
2. `ne_canvas` — done, wired into `libne`.
3. `ne` core (ne.h / ne_internal.h / ne.c) — done, wired into `libne` (compiles + links).
4. Bridge: adapt graph/serialize/eval to ne; replace editor/editor.c. DONE (agent ne-bridge; verified build + 54 self-checks + app --frames 60 exit 0).
5. Extras on top (not in upstream): DONE. Undo/redo (in bridge), clipboard (core subgraph serialize by serialize + editor wiring by ne-bridge), node/pin/link context menus, spawn search filter, mini-map. All phases complete: full imgui-node-editor C23 port + blueprint extras.

Upstream sources are staged at /tmp/ne/ (imgui\_\*.{h,cpp,inl}) for subagents.

## Survives / superseded

Survives: core/graph, core/serialize (yyjson), core/eval, core/node_desc,
core/pin_value, reflect + generated registry, demo/pin_fmt.c,
editor/eval_view.c, app/demo_graph.c, test/self_check.c.

Superseded: editor/editor.c's interaction code (pan/zoom/selection/undo/
marquee/links) — replaced by the port. Undo/redo is rebuilt on top of the
port; demo's value-label drawing moves into the port's node-render callback.
