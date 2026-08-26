# Research: SpartanEngine editor and workflows

## Scope

Read-only inspection of `PanosK92/SpartanEngine` on the `master` branch. The repository tree API returned tree SHA `86fe6b635ead1167b40f4e0eb731aea23f526c32`; source links below use the exact repository paths. This report is kept as editor-reference notes, not as a runtime dependency.

## Summary

Spartan's editor is an immediate-mode Dear ImGui shell built around a generic `Widget` lifecycle, a persistent dockspace, and a central widget registration list. The most reusable patterns are the `FileDialog` asset browser, `WorldViewer`'s explicit hierarchy selection and drag/drop rules, `Properties`' component-section dispatch, and self-contained typed drag/drop payloads.

The main adaptation risks are concrete: the inspector has no undo transaction handling, several editor systems use global/static state or raw pointers, asset thumbnails use asynchronous tasks that capture `this`, input blocking honors text-entry state rather than full ImGui mouse/keyboard capture, and the wiki's Lua callback argument description does not match `Script.cpp`. The engine is MIT licensed, but bundled fonts carry separate OFL licenses and third-party dependencies need their own notices.

## Findings

### 1. Editor shell, panel lifecycle, and docking

- `source/editor/Editor.cpp` owns the frame order: initialize the engine and ImGui, register widgets, begin an ImGui frame, tick the engine, draw the root dockspace, tick every widget, draw the menu/general windows, and render. When the editor is hidden it explicitly calls `Input::SetBlockedByUi(false)`. [Editor.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/Editor.cpp)
- `source/editor/EditorWidgets.cpp` registers 17 panels in a fixed order, including `WorldViewer`, `Properties`, `AssetBrowser`, `ScriptEditor`, `Viewport`, `Console`, `Sequencer`, and profiling/resource viewers. A new panel can follow the same `AddWidget<T>()` integration point. [EditorWidgets.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/EditorWidgets.cpp)
- `source/editor/widgets/Widget.cpp` provides a useful lifecycle: `OnTick()` always runs; visible windows then get size/constraint setup, `OnPreBegin()`, `ImGui::Begin()`, `OnVisible()` or `OnInvisible()`, `OnTickVisible()`, `ImGui::End()`, style restoration, and CPU profiling. `Widget.h` keeps title, visibility, docking, alpha, padding, and toolbar metadata in one base type. [Widget.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Widget.cpp) [Widget.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Widget.h)
- `source/editor/EditorLayout.cpp` creates one borderless full-work-area root window and an ImGui dockspace. Default splits are right `17%`, right-lower `60%`, bottom `22%`, and bottom-right `30%`. `editor.ini` persistence is configured in `EditorImGui.cpp`; the default layout is only rebuilt when the root dock node is absent. [EditorLayout.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/EditorLayout.cpp) [EditorImGui.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/EditorImGui.cpp)
- Borrow this separation: panels own content, `Widget` owns lifecycle, and layout owns docking. Severity low: only five `WidgetDock` values are handled, and a widget with another dock value is not assigned by the default builder.

### 2. Dear ImGui setup and input boundaries

- `source/editor/EditorImGui.cpp` enables keyboard navigation, docking, edge resizing, optional multi-viewports, and `editor.ini` settings. It loads Inter Regular/SemiBold at `14 * Window::GetDpiScale()`, forwards every SDL event to ImGui, and runs the SDL and RHI backends. [EditorImGui.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/EditorImGui.cpp)
- `begin_frame()` sets `Input::SetBlockedByUi(io.WantTextInput)`. `source/input/Input.cpp` then returns false from `GetKey`, `GetKeyDown`, and `GetKeyUp` when blocked. Mouse delta, wheel, and viewport membership are masked in `InputMouse.cpp`, but mouse positions and polled button states are not uniformly masked. [Input.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/input/Input.cpp) [InputMouse.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/input/InputMouse.cpp)
- `ImGuiSp::editor_shortcuts_blocked()` in `source/editor/imgui/ImGui_Extension.h` adds two gates that the global input block does not: engine `Playing` mode and a camera with `CameraFlags::IsControlled`. `WorldViewer` uses this helper before editor shortcuts. [ImGui_Extension.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_Extension.h) [WorldViewer.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp)
- `Viewport.cpp` relies on `ImGui::IsItemHovered()`, `ImGui::IsWindowFocused()`, gizmo gates, and editor shortcut gates. It does not check `ImGui::GetIO().WantCaptureMouse` or `WantCaptureKeyboard`. Severity medium: a port should define a deliberate capture policy instead of assuming `WantTextInput` covers all widget interaction. [Viewport.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Viewport.cpp)
- `InputKeyboard.cpp` is a fixed SDL scancode-to-array mapping; `InputGamepad.cpp` handles a single detected gamepad with an `8000` axis deadzone and rumble; neither provides a rebinding data model. `GeneralWindows.cpp` displays hard-coded keyboard/gamepad control tables, which are useful documentation but not runtime bindings. Severity medium: controls can drift from displayed documentation and cannot be remapped through the shown UI. [InputKeyboard.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/input/InputKeyboard.cpp) [InputGamepad.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/input/InputGamepad.cpp) [GeneralWindows.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/GeneralWindows.cpp)

### 3. Content and asset browser

- `source/editor/widgets/AssetBrowser.cpp` composes two `FileDialog` instances: an embedded all-file browser titled `Assets`, and a standalone model-selection dialog. The browser adds an `Import Model` toolbar action. Selecting a supported model opens a modal with cleanup, scale normalization, mesh combining, embedded-light import, and geometry optimization flags; import is queued through `ThreadPool::AddTask()`. Clicking an engine material calls `Properties::InspectMaterial()`. [AssetBrowser.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/AssetBrowser.cpp)
- `source/editor/widgets/FileDialog.h` is a reusable browser/file-picker interface. It separates browser/file-selection type, open/load/save operation, all/world/model/image filters, grid/list view, callbacks, toolbar actions, history, sorting, renaming, and directory watching. [FileDialog.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.h)
- `FileDialog.cpp` is a substantial borrowable pattern:
  - `ShowTop()` provides back/forward/up/refresh buttons, breadcrumbs, grid/list toggles, an icon-size slider, search, and file filters.
  - `RenderGridView()` uses an `InvisibleButton` card, draw-list selection/hover decoration, aspect-preserving atlas or thumbnail images, ellipsized labels, and drag sources.
  - `RenderListView()` uses a sortable/resizable three-column table for name, type, and modified time.
  - `DialogUpdateFromDirectory()` assigns atlas icons by file type and schedules supported image thumbnails with `ThreadPool::AddTask()`, `ResourceCache::Load<RHI_Texture>()`, and `PrepareForGpu()`.
  - `ItemContextMenu()` supports inline rename, reload script, delete, and open in explorer. `EmptyAreaContextMenu()` creates folders, Lua scripts, materials, and refreshes the view. [FileDialog.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.cpp)
- Directory changes are detected by `WatchDirectory()` polling `last_write_time` every 500 ms. Search filters labels with `ImGuiTextFilter`; list sorting keeps directories first. The browser accepts an `ENTITY` payload and saves a dragged entity as `<current directory>/<ObjectName>.prefab` through `Prefab::SaveToFile()`. [FileDialog.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.cpp)
- Borrow the separation of discovery, item rendering, callbacks, and context-menu actions. Severity medium: image tasks capture `this` without a visible lifetime guard, append after the initial sort, and do not consistently mark the dialog dirty; mtime polling can miss changes and ignores filesystem errors; no virtualization is shown for large directories. Breadcrumb parsing uses fixed 1024-byte buffers, and typed payload paths use fixed 512-byte buffers. [FileDialog.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.cpp) [ImGui_Extension.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_Extension.h)
- There are two entity payload conventions to account for when borrowing code. `WorldViewer` and `FileDialog` use the raw uppercase type `"ENTITY"` plus an exact `uint64_t`; `ImGui_Extension.h`'s typed enum maps `DragPayloadType::Entity` to `"Entity"`. Severity low/medium: standardize one convention before connecting panels. [WorldViewer.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp) [FileDialog.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.cpp) [ImGui_Extension.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_Extension.h)

### 4. Scene hierarchy

- `source/editor/widgets/WorldViewer.cpp` keeps hierarchy policy explicit. `should_show_in_tree()` hides inactive entities but keeps transient entities visible. `prepare_entity_filter()` retains matching ancestors when descendants match, counts active entities, and skips traversal while `ProgressTracker::IsLoading()` because loader workers mutate the hierarchy. [WorldViewer.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp)
- Selection is finalized on mouse release only when the pointer remains over the same row. Plain click replaces selection, Ctrl-click toggles, Shift-click selects an inclusive range in cached depth-first tree order, double-click focuses the selected entity, and empty-space click clears selection unless Ctrl is held. Selected descendants auto-expand their ancestors; active filtering forces matching parents open.
- Row interaction uses manually drawn highlight channels rather than ImGui's default selected/header colors. Inline rename uses `InputText` with `EnterReturnsTrue | AutoSelectAll`; Enter or deactivation commits a nonempty changed name, Escape cancels.
- Drag/drop uses an exact `uint64_t` entity ID under the raw type `"ENTITY"`. Row middle zones parent an entity; top/bottom zones set before/after reorder targets. Same-parent children use `MoveChildToIndex()`, root entities use `World::MoveRootEntityNear()`, and a window-level drop with no target unparents. Prefab payloads instantiate a new entity with `Prefab::LoadFromFile()`, `SetPrefabFilePath()`, and `MarkPrefabBaseline()`. [WorldViewer.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp)
- `HandleKeyShortcuts()` provides delete through `CommandEntityDelete`/`CommandStack`, Ctrl+L world load, Ctrl+Z undo, Ctrl+Shift+Z redo, and a primary-entity copy/paste clone path. `Viewport.cpp` separately implements Ctrl+D for cloning all selected entities. [WorldViewer.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp) [Viewport.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Viewport.cpp)
- Borrow the explicit selection state machine and the separation between row targets and window-level targets. Severity medium: traversal copies child vectors in some places because loading mutates them, but hierarchy pointers remain raw; filter expansion is forced every frame; visible selection excludes inactive entities; and hierarchy reparent/reorder operations shown here are direct mutations rather than clearly routed through the command stack. `HandleKeyShortcuts()` checks left Ctrl in the shown paths, while selection checks both left and right Ctrl, so shortcut behavior is asymmetric.
- Context-menu display does not have its own play-mode guard. The tree is wrapped in `BeginDisabled()` while playing and `SetSelectedEntity()` rejects selection, but any new context action should add an explicit mode check rather than rely on disabled drawing. Severity medium. [WorldViewer.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp)

### 5. Inspector and properties panel

- `source/editor/widgets/Properties.h` defines one renderer per supported component: `ShowEntity`, `ShowScript`, `ShowLight`, `ShowRender`, `ShowPhysics`, `ShowMaterial`, `ShowCamera`, `ShowTerrain`, `ShowSpline`, `ShowSplineFollower`, `ShowAudioSource`, `ShowVolume`, `ShowParticleSystem`, `ShowWater`, `ShowSpawnPoint`, `ShowCarReset`, and `ShowText3D`. Public static `InspectMaterial()` and `ClearMaterialInspection()` let other panels request standalone material inspection. [Properties.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.h)
- `Properties.cpp` reads selection from `World::GetCamera()`. Multiple selection shows names and snap actions but no per-property multi-edit. A single selection dispatches component sections in a fixed order. `component_begin()` creates a collapsible styled header, optional gear menu, and child content region; entity IDs are pushed for stable ImGui IDs. [Properties.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp)
- Common property helpers copy getter values into locals, draw `property_float`, `property_toggle`, `property_combo`, `property_text`, vector/transform, or resource controls, then call setters only after a change. `property_transform()` keeps Euler display state in static maps keyed by entity address and writes local position, rotation, and scale. This is a useful immediate-mode editing pattern, but the address-keyed state needs cleanup if entities are destroyed or addresses reused. Severity low/medium. [Properties.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp)
- The add-component context menu constructs typed components directly. Component context menus support `Remove Component`, `Copy Attributes`, and same-type-only `Paste Attributes`. Removal is deferred through `pending_removal_id` and `pending_removal_owner` until all `Show*()` calls finish, avoiding destruction while a component renderer is still on the stack. This deferred-removal pattern is worth borrowing. [Properties.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp)
- The panel is wrapped in `ImGui::BeginDisabled()` while `EngineMode::Playing` is set. File prefabs support `Update Prefab`, `MarkPrefabBaseline()`, and `Detach from Prefab`; code-prefab attributes are read-only; Save as Prefab writes under the project `prefabs/` directory. Materials support standalone inspection, render material overrides, browse/clear, typed drops, UV properties, and `Material::SaveToFile()`. [Properties.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp)
- `ShowScript()` shows a read-only script path with a browse control, reloads a selected engine Lua file, displays Loaded/Not Loaded, and edits top-level Lua booleans, integers, floats/doubles, and strings. Tables, functions, and unknown values are read-only labels. The string editor uses `char buffer[256]`, so values are limited to 255 characters in this UI. Nonempty Lua drag/drop calls `LoadScriptFile()` without the explicit engine-extension check used by the browse callback. Severity medium. [Properties.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp)
- Inspector limitations are material to a port: no undo/redo or transaction handling appears in `Properties.cpp`; `inspected_material` is a `weak_ptr` and can disappear when the resource expires; `copied_component` and deferred-removal owner state are raw pointers; Save-as-Prefab uses another fixed 256-byte name buffer. Severity high for tools that require reliable inspector undo, medium for lifetime and buffer issues. [Properties.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp)

### 6. Lua behavior and scripting workflow

- The documented workflow is: right-click in the file browser, choose `New Lua script`, add a `Script` component to an entity, assign the file, then edit/reload it. The wiki documents Lua 5.4, `Timer`, `World`, `Entity`, math types, selected component APIs, and examples. [Scripting wiki](https://github.com/PanosK92/SpartanEngine/wiki/Scripting)
- `FileDialog.cpp` embeds the new-script template. It returns a table and includes `Start(Entity)`, `Stop(Entity)`, `Remove(Entity)`, `PreTick(Entity)`, `Tick(Entity)`, `Save(Entity)`, and `Load(Entity)`. The template links back to the scripting wiki. [FileDialog.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.cpp)
- `source/world/components/Script.cpp` requires `safe_script_file()` to succeed and the file to return a Lua table. `LoadInternal()` applies XML attributes to known primitive table fields, then calls `Initialize(script, entity)` and `Load(script, entity, saved_data)`. `Start`, `Stop`, and `Remove` call the matching hook with `(script, entity)`. `PreTick` and `Tick` run only in playing and not paused mode. `Save` serializes the script table and any table returned by `Save`. Deferred world loading queues script initialization on the main thread, with light entities ordered first. [Script.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/world/components/Script.cpp) [Script.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/world/components/Script.h)
- Severity high, documentation/source mismatch: the wiki says colon-syntax callbacks receive the owning Entity as `self` and shows `Start()`/`Tick()` without an explicit entity parameter, while the C++ source passes the script table as the first Lua argument and `GetEntity()` as the second. The embedded template's explicit `Entity` parameter matches the C++ call more closely. Verify and unify this convention before borrowing the workflow. [Scripting wiki](https://github.com/PanosK92/SpartanEngine/wiki/Scripting) [Script.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/world/components/Script.cpp)
- Severity medium: `LoadInternal()` calls `Initialize`, and the component also exposes `Script::Initialize()`, so the source path can invoke initialization twice depending on the world lifecycle. Missing files return silently. Only `Tick`, `Load`, and `Save` report protected-function errors; `Initialize`, `Start`, `Stop`, `Remove`, and `PreTick` ignore their results. Serialization supports only string-keyed `int`, `float/double`, `bool`, and `string` values, not nested tables or arrays. [Script.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/world/components/Script.cpp)
- `source/editor/widgets/ScriptEditor.cpp` is a lightweight auxiliary editor, not a complete asset authoring workflow. It recursively scans `std::filesystem::current_path()` for `.lua` files whenever visible, loads selected text into an embedded Lua `TextEditor`, offers Reload and Open, and explicitly recommends VSCode with Sumneko's Lua extension. There is no visible Save button or file-write call in this source. Severity medium/high for a self-contained editor. [ScriptEditor.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/ScriptEditor.cpp) [ScriptEditor.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/ScriptEditor.h)

### 7. Reusable ImGui helpers and payload design

- `source/editor/imgui/ImGui_Extension.h` defines typed payload kinds for texture, entity, model, audio, material, Lua, and prefab. `DragDropPayload` stores full and relative paths in 512-byte arrays; `create_drag_drop_payload()` submits the whole struct; receivers require `DataSize >= sizeof(DragDropPayload)`. `receive_drag_drop_payload_rect()` accepts before delivery but returns data only on left-button release. [ImGui_Extension.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_Extension.h)
- The self-contained path payload is a good way to survive asset-list refreshes, but paths truncate at 511 characters and receivers cast ImGui-owned payload memory without an ownership/copy layer. Severity medium for long or asynchronous paths.
- `source/editor/imgui/ImGui_EditorUi.h` supplies DPI scaling, animated ImGui state storage, cards, chips, section headers, primary-button style, table/row styling, toolbar sizes, and draw-list selection visuals. These helpers give panels a common visual vocabulary without a separate retained UI layer. [ImGui_EditorUi.h](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_EditorUi.h)

### 8. License, dependencies, and project constraints

- `license.md` is the standard MIT License, copyright 2015-2026 Panos Karabelas. Source files also carry the MIT header, and the README describes the project as free to use with attribution. [license.md](https://github.com/PanosK92/SpartanEngine/blob/master/license.md) [README.md](https://github.com/PanosK92/SpartanEngine)
- `GeneralWindows.cpp` embeds the MIT text in the About window and exposes a third-party table with links for ImGui, Lua, Sol2, Assimp, FreeType, SDL, PhysX, RenderDoc, Vulkan-related libraries, and others. The engine MIT file should not be treated as a replacement for each dependency's license. [GeneralWindows.cpp](https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/GeneralWindows.cpp)
- Bundled `data/fonts/Inter/OFL.txt` and `data/fonts/OpenSans/OFL.txt` carry SIL Open Font License 1.1 notices. If borrowing the editor fonts, preserve those notices and do not relicense the fonts as MIT. [Inter OFL](https://github.com/PanosK92/SpartanEngine/blob/master/data/fonts/Inter/OFL.txt) [Open Sans OFL](https://github.com/PanosK92/SpartanEngine/blob/master/data/fonts/OpenSans/OFL.txt)
- The maintainer's Qt-to-ImGui rationale in issue #13 is that ImGui is MIT licensed, fast to develop with, easy to maintain, and platform independent. This supports borrowing the integration pattern, not assuming that all bundled assets share the engine license. [Issue #13](https://github.com/PanosK92/SpartanEngine/issues/13)
- The Building wiki says project generation supports Vulkan on Windows, D3D12 on Windows, and Vulkan on Linux, and first run downloads hundreds of megabytes of precompiled libraries from Dropbox. It says the Linux build can boot and render the editor but should be used to help finish it. [Building wiki](https://github.com/PanosK92/SpartanEngine/wiki/Building)
- CI currently builds on `windows-2022` with VS 2022, x64, and `v143`; it covers Vulkan development/release/debug and D3D12 release, with no Linux/macOS build, D3D12 debug/development build, test, lint, or static-analysis job shown. Severity medium for portability and confidence. [workflow.yml](https://github.com/PanosK92/SpartanEngine/blob/master/.github/workflows/workflow.yml)
- The README explicitly calls the repository a personal R&D engine rather than a commercial product and says there is no support queue or roadmap promise. Treat the editor code as an implementation reference, not as a supported editor framework. [README.md](https://github.com/PanosK92/SpartanEngine)

## Patterns worth borrowing

1. Use a small panel base class with explicit visible lifecycle hooks, then keep layout and panel content separate. Start with `Widget.cpp`, `Editor.cpp`, and `EditorLayout.cpp`.
2. Build the content browser as a reusable file-dialog core with separate discovery, cached icon/thumbnail selection, view rendering, callbacks, context menus, and typed drag/drop. Start with `FileDialog.h/.cpp` and `AssetBrowser.cpp`.
3. Model hierarchy interactions as explicit states: filtered IDs, primary/multi/range selection, rename state, row parenting zones, reorder target, and window-level unparent target. Start with `WorldViewer.cpp`.
4. Use component-specific inspector functions and defer removal until after rendering. Add a command/transaction layer before using the `Properties.cpp` editing pattern in a production tool.
5. Use path-bearing drag/drop payloads rather than pointers to asset-vector entries, but replace fixed arrays with validated owned strings if long paths or asynchronous work matter.
6. Treat script API documentation and source invocation as one contract. Resolve the callback argument mismatch and initialization ordering before copying the Lua template or wiki examples.
7. Define a complete input capture policy using ImGui mouse and keyboard capture flags, game/editor mode, camera control, and text editing. Do not rely on `WantTextInput` alone.

## Gaps and residual risks

- No build, runtime smoke test, UI screenshot, or performance benchmark was run. This was intentional read-only web research.
- The relevant editor, input, script, license, README, wiki, and CI files were inspected, but the entire dependency and data-asset license surface was not audited.
- The local read-only reference clone is `/Users/cwaldner/Personal/reference-engines/LumixEngine` at commit `1baba8d`; it is separate from this repository and no source files were copied verbatim.
- Links use `master`, so source may move after this report. Pin borrowed code to a commit in a downstream project.
- The report does not establish whether all direct hierarchy mutations are undoable elsewhere in the engine; it only records that the shown `Properties.cpp` has no undo handling and the shown hierarchy mutations are direct.
- `ScriptEditor.cpp` has no visible save call, but the embedded `TextEditor` implementation was not audited for hidden file-writing behavior.

## Additional references

These projects are comparison references, not code dependencies. Borrow the interaction contracts and module boundaries; do not copy implementation text without checking the source license and preserving notices.

- Lumix Engine is MIT licensed and uses Dear ImGui in its editor. `src/editor/asset_browser.cpp` separates discovery, cached thumbnails, filters, grid/list presentation, context actions, and resource callbacks. `src/editor/property_grid.cpp` dispatches typed reflected properties and defers destructive component removal until after rendering. `src/editor/studio_app.cpp` keeps hierarchy selection, rename, drag/drop, and editor shortcuts as explicit state. A separate local reference clone is pinned at `/Users/cwaldner/Personal/reference-engines/LumixEngine`, commit `1baba8d`. Repository: <https://github.com/nem0/LumixEngine>
- Hazel is Apache-2.0 licensed. Hazelnut separates `SceneHierarchyPanel.cpp`, `PropertiesPanel.cpp`, `ContentBrowserPanel.cpp`, and `EditorLayer.cpp`. The split is a useful model for keeping selection, inspection, asset browsing, and shell orchestration distinct. Repository: <https://github.com/TheCherno/Hazel>
- ArcGameEngine is Apache-2.0 licensed and uses Dear ImGui with ImGuizmo. Its editor separates scene hierarchy, properties, asset browsing, and editor-layer orchestration. Repository: <https://github.com/GloriousPtr/ArcGameEngine>
- SpartanEngine is MIT licensed and has a useful `EditorLayout.cpp` plus widget lifecycle around Dear ImGui docking. Its file browser demonstrates a responsive grid/list choice, search, breadcrumbs, icon cards, drag/drop, and context menus. Repository: <https://github.com/PanosK92/SpartanEngine>
- SparkEngine is another Dear ImGui editor reference with scene, inspector, asset, visual scripting, AI, undo, and play-mode surfaces. Its project license is separate from the MIT/Apache examples above and must be reviewed before borrowing code. Repository: <https://github.com/Krilliac/SparkEngine>

For c-blueprints, the Content Drawer adapts the interaction model in Lumix's `asset_browser.cpp`: it scans the configurable content root, opens at the root directory, shows breadcrumbs and back/forward history, lists folders before files, and supports icon Grid and List views. Search matches source paths case-insensitively, single click selects, and double click opens or enters the item. The drawer uses icon cards rather than resource thumbnails. Drag/drop, a persistent asset index, resource previews, and file rename/delete remain deferred. Use `build/app --headless --screenshot` after each UI slice.

## Sources

Kept:

- Repository README: <https://github.com/PanosK92/SpartanEngine>
- License: <https://github.com/PanosK92/SpartanEngine/blob/master/license.md>
- Editor loop: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/Editor.cpp>
- ImGui setup: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/EditorImGui.cpp>
- Dock layout: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/EditorLayout.cpp>
- Widget base: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Widget.cpp>
- Asset browser: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/AssetBrowser.cpp>
- File browser: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.cpp>
- File browser interface: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/FileDialog.h>
- Scene hierarchy: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/WorldViewer.cpp>
- Inspector: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Properties.cpp>
- Script editor: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/ScriptEditor.cpp>
- Script component: <https://github.com/PanosK92/SpartanEngine/blob/master/source/world/components/Script.cpp>
- Scripting documentation: <https://github.com/PanosK92/SpartanEngine/wiki/Scripting>
- Input abstraction: <https://github.com/PanosK92/SpartanEngine/blob/master/source/input/Input.cpp>
- Keyboard input: <https://github.com/PanosK92/SpartanEngine/blob/master/source/input/InputKeyboard.cpp>
- Mouse input: <https://github.com/PanosK92/SpartanEngine/blob/master/source/input/InputMouse.cpp>
- Viewport interaction: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/widgets/Viewport.cpp>
- ImGui extensions and payloads: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_Extension.h>
- ImGui editor styling: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/imgui/ImGui_EditorUi.h>
- About/license/dependency UI: <https://github.com/PanosK92/SpartanEngine/blob/master/source/editor/GeneralWindows.cpp>
- Build constraints: <https://github.com/PanosK92/SpartanEngine/wiki/Building>
- CI: <https://github.com/PanosK92/SpartanEngine/blob/master/.github/workflows/workflow.yml>
- Qt-to-ImGui rationale: <https://github.com/PanosK92/SpartanEngine/issues/13>
- Inter font license: <https://github.com/PanosK92/SpartanEngine/blob/master/data/fonts/Inter/OFL.txt>
- Open Sans font license: <https://github.com/PanosK92/SpartanEngine/blob/master/data/fonts/OpenSans/OFL.txt>

Dropped:

- Search-result summaries and third-party engine directories were not used as evidence when the repository source or official wiki was available.
- UI proposal/pull-request pages were not used to describe current behavior because they describe proposed or historical changes rather than the inspected `master` files.

## Acceptance report

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "This report records concrete editor, browser, hierarchy, inspector, scripting, input, license, and build findings with repository file paths, exact source URLs, and severity labels for material limitations."
    }
  ],
  "changedFiles": [
    "docs/editor-engine-references.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [],
  "validationOutput": [
    "Read-only web/API inspection completed; no SpartanEngine files were edited and no build or tests were run."
  ],
  "residualRisks": [
    "master-branch links can move after this report",
    "no runtime build or UI validation was performed",
    "the complete third-party and data-asset license surface was not audited",
    "the embedded TextEditor implementation was not inspected for hidden save behavior"
  ],
  "noStagedFiles": true,
  "diffSummary": "Kept the SpartanEngine inspection and added comparison notes for Lumix, Hazel, Arc, and Spark; no third-party repository files were changed.",
  "reviewFindings": [
    "high: source/editor/widgets/Properties.cpp - inspector editing has no visible undo or transaction layer",
    "high: source/world/components/Script.cpp and wiki/Scripting - callback argument convention is inconsistent",
    "medium: source/editor/widgets/FileDialog.cpp - asynchronous thumbnail tasks capture this without a visible lifetime guard",
    "medium: source/editor/widgets/Viewport.cpp and source/editor/EditorImGui.cpp - full ImGui mouse/keyboard capture is not explicitly honored",
    "medium: source/editor/widgets/ScriptEditor.cpp - no visible save operation and recursive current-working-directory scan",
    "medium: source/world/components/Script.cpp - initialization and error-reporting behavior need verification"
  ],
  "manualNotes": "Borrowing should pin source to a commit and preserve MIT, OFL, and dependency notices separately."
}
```
