# Editor usability review

Current direction supersedes the original transport and breadcrumb recommendations
below: Scene and Graph retain floating controls, shown on hover or focus with
200 ms pointer transitions; the menu bar has no transport buttons. The Scene menu
selects the shared target. The breadcrumb strip is removed, while Navigator keeps
Back/Forward. See [implementation progress](implementation-progress.md) and the
[MathExpression reference](../../math-expressions.md).

September 6, 2026. Review of checkout `90fc6e6f` before a UI refactor.

The follow-up [Inspector refinements](inspector-refinements.md)
updates the picker proposal to searchable multiselect, places axis labels outside
their colored borders, specifies retained settings on removal, and assesses
editor-created component definitions. Those refinements supersede the original
picker/removal recommendations below.

Keep ImGui for this refactor. The largest usability problems come from unclear ownership of actions, inconsistent selection behavior, and unnecessarily large property and node layouts. The current renderer already draws custom nodes, controls, sidebars, and transitions. None of the reported visual changes requires a different layout library.

The first implementation slice should make the editor predictable: one playback toolbar, explicit editing and debugging targets, consistent asset selection, and compact Inspectors. Changing the theme alone would leave the most frustrating interactions intact.

## Evidence and limits

I read the supplied objective and navigation reference image, the Emil design engineering and Apple design skills, the existing editor design document, and the implementation of navigation, Inspector fields, graph rendering, runtime inspection, prototype preview, and generated entity access. I also read Apple's component index, split views, toolbars, sidebars, search fields, and outline views in a JavaScript-capable browser, plus the Clay and Dear ImGui repositories.

I built the current checkout and inspected fresh captures from startup and the existing scripted native UI workflows. These are developer walkthroughs and code-backed findings, not a study with independent users. Native computer-use tooling did not recognize the unbundled executable, so the visual walkthrough uses the app's own screenshot support and native integration runner. The exact reported bubble failure remains unreproduced.

`make build` passed. The initial `make test` failed at floating-Inspector checks 3088, 3090, 3091, 3094, and 3095 while a separate editor process was open. Their generic log label mentions selection and deletion, but the failing section concerns undocked Inspector movement and collapse controls. After closing that process, the isolated full `make test` passed, including the native UI suite and resize-pane assertion. This suggests a test-environment sensitivity; it does not establish its cause. No application source or authored content was changed for this review.

The review's transient screenshots and raw test transcripts have been removed; the findings and validation results are preserved in the Markdown documents.

## The proposed interaction model

Use a stable workspace with Navigator on the left, document views in the center, and Inspector on the right. Scene, Graph, and Prototype Preview can share the center through tabs or splits. Preserve simultaneous Scene and Graph views. Offer a balanced split as the initial layout, and remember each user's sizing and graph camera afterward. The current 68% graph-height default is a poor starting point when someone wants to see gameplay.

Place sidebar toggle, Back, and Forward together at the leading edge of the workspace toolbar, following the supplied reference. Place a labeled runtime target and one transport group in the same toolbar. Keep Inspector visibility at the trailing edge. Local view controls such as Frame Selection, Fit Graph, camera options, and Arrange stay in their own view header. Apple recommends grouping commands by purpose and keeping navigation in a familiar leading location. [Apple toolbars](https://developer.apple.com/design/human-interface-guidelines/toolbars)

Keep four concepts separate in behavior and visible naming:

| Concept | Example | What changes it |
| --- | --- | --- |
| Editing document | Player prototype | Opening or editing an asset |
| Selection | Player_1, a component, or a graph node | Clicking an item or returning through navigation history |
| Runtime target | Scene: NeonSwarm, or Preview: Projectile | Explicit runtime target choice |
| Debug target | Player_1 / InputBehavior | Selecting or pinning a live behavior instance |

Clicking a graph must not retarget playback. Browsing another prototype must not stop a running scene. Clicking empty scene space may clear scene selection, but should not silently erase a pinned debug target. Save and Undo act on the editing document, with its name available in the command and document header.

Single-click an asset to select it and show its Inspector. Double-click or Enter opens its editor view. For prototypes, selection also updates a static, framed preview if the preview area is visible; opening a prototype reveals that area. Neither action automatically runs its behaviors. This gives the requested automatic preview without sending a projectile away before it can be inspected.

Navigation history stores document identity, graph/function identity, selection, and view position. Back restores the previous location, Forward restores the next, and a new navigation after Back discards the forward branch. Field edits, pan frames, and hover do not generate history entries. Keep breadcrumb chevrons for ancestry; Back and Forward retrace visited locations rather than pretending that ancestry is history.

## Findings and changes

P1 means it blocks a common workflow or makes the editor misleading. P2 means frequent friction or readability cost. P3 is finishing detail. “Confirmed” below means supported by the cited code or captured state; it does not imply every possible interaction was exercised manually.

| Priority | Before | After | Why and evidence |
| --- | --- | --- | --- |
| P1 | Scene and Graph each show Play, Stop, and Step for the same active target. | One workspace transport with an explicit Scene/Preview target. Scene and Graph retain local view controls. | Duplicated placement implies independent simulations. Confirmed in startup capture and shared state in scene_authoring.c:5550 (`app/scene_authoring.c:5550`; historical repository reference). |
| P1 | A running scene can show “Idle” in an Inspector runtime section, and graph inspection depends on the selected entity and matching behavior. | Show session state beside transport and “Inspecting Player_1 / InputBehavior” in the graph. Use “Waiting for Input” or “No active execution” where appropriate. Add a debug target picker and pin. | Current labels describe different lifecycles without explaining their scope. Attachment selection (`app/scene_authoring.c:651`; historical repository reference), trace binding (`app/scene_authoring.c:839`; historical repository reference), and running capture. |
| P1 | Files under gameplay are visible and have an Open action, but selecting them only changes the highlighted path and opening them falls through. | Recognize gameplay data as an asset kind. Show name, kind, path, validation, and fields; support Save, Revert, Undo, and a defined Apply action when runtime integration exists. | Confirmed missing route in select/open handlers (`app/scene_authoring.c:3244`; historical repository reference). Existing authored-data API (`game/authored_data.h:22`; historical repository reference) provides persistence and validation, but the editor must connect it and define which runtime consumes it. |
| P1 | Switching the selected prototype clears prototype Undo and Redo history. | Retain history per stable prototype identity for the lifetime of the editing session. | Navigation should not remove the ability to undo an edit. Confirmed explicit clear in select_prototype (`app/scene_authoring.c:679`; historical repository reference). Test edit A, select B, return to A, Undo. |
| P1 | Prototype preview requires a button and immediately launches a running entity in the shared viewport. | Automatic still preview in a named Prototype Preview view, with explicit Simulate and replay controls when needed. | Selection, inspection, and simulation are different intents. Preview control (`app/scene_authoring.c:3837`; historical repository reference), launch (`app/scene_authoring.c:1757`; historical repository reference). |
| P1 | Add-node results have no deliberate height limit or separate scrolling result region. The focus request precedes Add Comment Box rather than the input. | Anchored popup, search first and focused on open, bounded scrollable results, selected result kept in view. | Confirmed in spawn_menu (`editor/editor.c:4964`; historical repository reference). Focus placement is a code-level defect candidate, not a manually reproduced input trace. |
| P2 | Numeric property labels sit above full-width inputs. Components default closed, with 32-pixel disclosure rows. | Open components initially, with compact label/value rows. Preserve deliberate collapse choices by asset and component. | property_number (`app/scene_authoring.c:3728`; historical repository reference) and custom disclosure (`editor/ui_shell.c:740`; historical repository reference). The prototype's SetNextItemOpen call does not control this custom storage-backed disclosure. |
| P2 | Transform X and Y consume separate stacked fields. Structured values lack a general inline editor. | One coordinate row with labeled X and Y fields and colored left rules; include Z only for three-dimensional types. Reuse the pattern for vector-valued inputs. | Current Transform aliases the two-dimensional CF_V2 in components.h (`game/components.h:6`; historical repository reference). Do not introduce a fictitious Z value. |
| P2 | Add Component and Add Behavior sit after lists. Prototype behavior rows must expand to reveal Open Graph and Detach. | Put labeled Add actions in section headers. Make a behavior row open its graph directly; provide a separate disclosure only for real nested content. Use a searchable bounded picker. | Prototype components and behaviors (`app/scene_authoring.c:3848`; historical repository reference) and instance behavior rows (`app/scene_authoring.c:4710`; historical repository reference) already use different interaction patterns. |
| P2 | Remove Component is a full text button at the bottom of each expanded section. | Small trash action in the component header, revealed on hover or keyboard focus, with a stable hit target and Undo. | Saves space and places removal beside the thing it affects. Instance removal (`app/scene_authoring.c:4628`; historical repository reference). Keep the action keyboard reachable and also available in the component menu. |
| P2 | Search clearing lives in a separate adjacent X button. | Put the clear affordance inside one search field; show it only with content and keep focus in the field after clearing. | Shared field only draws the magnifier in ui_shell.c:717 (`editor/ui_shell.c:717`; historical repository reference); callers add external buttons in Files (`app/scene_authoring.c:5046`; historical repository reference) and Scene (`app/scene_authoring.c:4196`; historical repository reference). |
| P2 | Files distinguish folders and prototypes, while other files share a generic icon. | One restrained icon family for folders, scenes, prototypes, behaviors, systems, and gameplay data. Show type in search results when names collide. | Confirmed icon dispatch in draw_content_tree (`app/scene_authoring.c:4944`; historical repository reference). The Document outline already has richer icons; extend a consistent mapping rather than building a second system. |
| P2 | Most nodes use the same card structure, even for simple arithmetic. | Compact operator, getter, branch, and function presentations while retaining type and port identity. | Custom node drawing (`editor/editor.c:3810`; historical repository reference) already provides the required rendering control. See the node proposal below. |
| P2 | Creating a new Health struct is necessary to change one field through the available setter. | Add field setters or a “Set fields” node that updates only selected members, defaulting Target to Self. | HealthBehavior (`content/behaviors/HealthBehavior.json:402`; historical repository reference) reconstructs the component. This is an authoring/API issue, not a theme issue. |
| P2 | Initial graph framing clips nodes while the default split devotes little height to Scene. | Frame the initial graph once after layout settles. Offer balanced Scene/Graph and single-view layouts; preserve deliberate pan/zoom. | Startup capture, startup graph opening (`app/main.c:515`; historical repository reference), split ratio (`editor/ui_shell.c:16`; historical repository reference). Do not continuously auto-fit while someone works. |
| P2 | Selection and open semantics differ by file kind. A behavior file can stay highlighted while the Inspector shows an older scene selection. | Every selected asset gets meaningful identity and properties; opening changes the document predictably. Preserve explicit Inspector pinning if added. | select_content_entry (`app/scene_authoring.c:3244`; historical repository reference) only special-cases prototypes, and prototype selection alone does not set the editing document. |
| P2 | Inspector chrome can scroll away while values remain visible. | Keep object name, asset/instance identity, and edit/runtime state in a fixed Inspector header; scroll the property body. | The running capture begins halfway through Health fields without the owning object title. This makes compactness and context equally important. |
| P3 | Breadcrumbs use literal slashes and a text Back button. | Small chevron separators; icon Back/Forward in the workspace navigation group. | Graph navigation (`editor/editor.c:5534`; historical repository reference) uses slashes and an ancestry-only Back operation. Keep full breadcrumb labels available on hover/focus when truncated. |
| P3 | Tab close marks, pane close marks, collapse arrows, and behavior removal X buttons compete visually. | One close action per applicable tab, distinct sidebar toggle icons, and trash/unlink semantics for removing content. Use smaller centered close glyphs with unchanged hit areas. | Startup capture and icon sizing (`editor/ui_shell.c:699`; historical repository reference). The screenshot supports the visual judgment; verify final optical sizing at both display scales. |
| P3 | Permanent hint copy such as “Colors are in Material.” uses property space. | Remove tutorial and hint text. Retain concise names, state, validation errors, and actionable failure messages. | Confirmed in prototype Appearance (`app/scene_authoring.c:3871`; historical repository reference) and instance Appearance (`app/scene_authoring.c:4594`; historical repository reference). Tooltips can name icon actions without teaching the workflow. |

## Runtime controls and graph activity

Use one Play/Pause, Stop, Step, and Restart group. Play starts or resumes the selected runtime target. Step advances one simulation frame only while paused. Graph debugger commands such as Step Node and Step Over belong in the optional debugger, with distinct labels. Otherwise the same Step icon can mean different units of work.

Target changes during a running session need a deterministic rule. For the first version, require Stop before selecting a different runtime target; keep the target label visible and disabled while running. Browsing files and opening graphs remain available. Preview follows the selected prototype when idle, and remains pinned to its simulated prototype until stopped.

Bubbles should communicate execution, with the executed branch visible and the untaken branch quiet. I recommend moving bubbles on exec edges only by default. Data edges indicate dependencies; animating them with identical moving dots suggests a second control flow and makes dense graphs hard to follow. Use watched values, last-value labels on demand, or a brief data-edge highlight. Offer data evaluation tracing as an explicit debugging option.

The current running Input capture has bubbles, including data bubbles. ed_draw_link (`editor/editor.c:3521`; historical repository reference) already uses a continuous editor clock for marker phase to avoid stationary bubbles during held input. Do not blindly “fix” that by restarting every bubble on every frame. This review has not verified the timing of isolated one-shot pulses or the user's failing sequence.

The important implementation checks are:

1. Bind the displayed graph to a clearly named live instance. selected_behavior_attachment (`app/scene_authoring.c:651`; historical repository reference) requires the selected entity to own the displayed behavior. Opening a shared asset alone does not identify which entity to inspect.
2. Preserve completed-event traversal records long enough to display. behavior_instance_release_activation (`core/instance.c:400`; historical repository reference) already copies them into the instance trace before recycling, so completed events are not universally untraced.
3. Inspect a one-shot Input event, held Input, a Branch with one untaken output, delayed execution, nested functions, and multiple live entities using the same behavior. Confirm that editor and runtime use the same clock and document revision.
4. For nested functions, verify that child-run traversals reach the displayed function graph. Edge identity must distinguish the owning asset/revision and graph where necessary. Graph/node IDs are shared within an asset, so the lack of a graph ID alone is not proof of an existing collision.
5. Avoid the graph-wide fallback pulse in ed_flow_style_for_edge (`editor/editor.c:3377`; historical repository reference) as evidence of a specific executed path. Its current caller is the separate movement-system authoring path, not proof of a defect in the captured Scene Input path.
6. With reduced motion, use static recent-execution emphasis or counts. Freeze a debug view meaningfully on pause and let stopped activity settle; do not keep suggesting ongoing execution.

If a live target is unavailable, show “No instance selected” in the debugger context. If the instance is alive but no event is active, show that precise state. Avoid calling both conditions “Idle.”

## Inspector and prototype preview

Use a shared property-row implementation for prototypes, scene instances, and graph defaults. Start with a 28-pixel row and 30-pixel section header at the current desktop scale, a label column near 40%, and a flexible value column. These are proposed starting values, not Apple-mandated dimensions. At narrow widths, wrap long labels and compound values without hiding them. Apply display scaling to geometry as well as fonts.

X, Y, and optional Z retain visible letter labels. Give each value cell a slim red, green, or blue left rule. Color reinforces the label; it must not be the only way to identify the axis. Keep text entry, paste, precision, and Undo reliable. Numeric dragging can be added, but must not replace direct typing.

Show Components and Behaviors as distinct sections with their Add action beside the title. Keep common component values expanded initially. Advanced material controls may remain collapsed. Reserve header action space so the title does not jump when the trash icon appears. Reveal removal on keyboard focus too. One removal should be one Undo operation; avoiding a confirmation is appropriate for this reversible local edit.

Adding a component should insert and reveal it immediately. Adding a behavior attaches an existing asset, so use the label “Attach Behavior” in the picker. “New Behavior” is a separate creation action. Selecting an attached behavior opens its graph. Preserve the prototype/instance distinction in the Inspector title and show local overrides beside their fields, with a compact revert action only where an override exists.

For projectile previews, use a small isolated stage that resets from authored defaults. Still view shows the projectile at its origin. Simulation can expose Loop, Follow, Restart, and speed. Loop should reset the entire preview world after a short bounded interval or entity death, so spawned particles and child entities do not accumulate. Follow keeps the subject framed while leaving a grid or trail that reveals movement. These options alter the preview presentation/session, never the projectile's authored velocity or lifetime.

Stop returns to the authored still pose. Do not silently respawn a projectile in the actual scene. If the preview requires a target or contact to demonstrate a behavior, expose a deliberate preview fixture rather than modifying the graph. A seekable timeline is later work because deterministic replay and snapshots are required; a decorative scrubber would promise more than the engine supplies.

## Graph simplification

| Before | After | Why |
| --- | --- | --- |
| Add, Multiply, and comparisons use a title band and verbose output labels. | Compact operator nodes with a recognizable symbol, visible input values when unconnected, and a stable type indication. Full searchable names remain available. | Less area and shorter wires without sacrificing type meaning. Keep a readable name at normal editing zoom where the symbol could be ambiguous. |
| Branch resembles a general function card. | Compact control-flow block with Condition and explicit True/False output labels. | Color and port position alone are insufficient to explain branch semantics. |
| Getters show full struct output even when only one member matters. | A compact getter with expandable member outputs, preserving the current field-output feature. | Existing field outputs (`test/field_outputs.c`; historical repository reference) already avoid many break-struct nodes. |
| Function calls, events, and operators lack a consistent title icon vocabulary. | Small category icons for events, functions, component reads/writes, and flow. Use the operator symbol itself for simple math. | Icons improve scanning only when the same icon means the same thing across the Navigator, picker, and canvas. |
| Unconnected scalar fields already support inline editing, but structures and entity selection are limited. | Keep existing scalar/color/choice controls; add vector editors and contextual entity or asset pickers. | ed_draw_value_editor (`editor/editor.c:1772`; historical repository reference) already supports scalar/string inputs but skips structures. This is an extension, not a new inline-editing subsystem. |
| Self and other-entity access are separate generated names. | Present “Get Health” and “Set Health” with an explicit Target, default Self, while reusing current generated operations internally. | GetEntity/SetEntity generation (`reflect/reflect.c:1574`; historical repository reference) and the shipped AutoWeapon graph already support other entities. Discovery and field granularity are the gaps. |
| Change Current Health by rebuilding Health with Current and Maximum. | “Set Health · Current” with Target and value, preserving all unselected members. | Prevents accidental overwrites and removes irrelevant wiring. Preserve execution ordering and reflected read/write effects. |
| Direction To is a local graph composed from math operations. | Consider a reusable native Direction To vector operation, or a shared pure function asset, with defined zero-distance behavior. | This is a stable mathematical operation. It is a reasonable engine utility, unlike hiding an entire weapon or damage policy in native code. |

Keep HealthBehavior's decisions visible: reject/clamp negative damage as intended, compute new health, enforce bounds, and destroy the entity at zero. A reusable editable “Apply Damage” graph is reasonable; a native canned node should not silently hard-code those game rules. The existing design document also emphasizes keeping weapon composition visible. Field setters remove ceremony while preserving that goal.

The add-node menu should open instantly for keyboard use. Start around 360 pixels wide and cap height to the smaller of 420 pixels and available viewport space. Keep the search at the top and only the results scrollable. Dragging a wire into empty space filters compatible nodes and preserves autowiring. Up/Down changes selection, Enter inserts once, Escape cancels and returns focus to the canvas. Include result grouping or type subtitles without repeating category headers for each fuzzy-ranked match. Names, aliases, categories, and compatible types should influence ranking consistently.

## Additional usability checks

The following need explicit acceptance checks during implementation. They are not all established defects:

- Keyboard access to Files, Scene, graph navigation, component actions, and all popup results. Typing in a property or search must never fire game input or graph shortcuts. Escape should cancel the nearest transient interaction first.
- Meaningful focus indication for custom invisible-button controls. Confirm names, roles, and values through an accessibility bridge; neither a custom ImGui canvas nor Clay layout automatically makes the editor screen-reader accessible.
- Long asset names, missing references, renamed files, no search results, and malformed gameplay data. An Open action must show a document or a specific error. Never leave an unrelated Inspector appearing to describe the selected file.
- Undo across document switches, multi-selection edits, component detach/reattach, prototype updates, and session Stop. Group a continuous property edit into one undo record.
- Visible dirty state and applied-versus-saved state. Saving a behavior may succeed while application to the runtime fails, as save_active_document (`app/scene_authoring.c:5207`; historical repository reference) already handles. Preserve that distinction in the document UI.
- Shared asset identity. Selecting gameplay/Player and prototypes/Player must make the different kinds obvious; neither icon nor basename alone is enough.
- Search ranking. Files computes a fuzzy score but uses it as a match predicate while iterating the tree, whereas node search sorts scores. Align behavior where it helps users predict results.
- Narrow windows, 1x/2x display scales, larger text, reduced motion, and high contrast. Use quiet opaque editor backgrounds first; transparent material should be added only if it improves hierarchy and preserves graph legibility.

Apple's outline guidance supports retaining expansion choices and truncating names thoughtfully. Its split-view guidance supports resizable panes, useful minimum sizes, and multiple ways to restore hidden panes. These apply directly to the Navigator and Inspector. [Apple outline views](https://developer.apple.com/design/human-interface-guidelines/outline-views), [Apple split views](https://developer.apple.com/design/human-interface-guidelines/split-views)

## ImGui versus Clay

| Decision area | Keep ImGui | Adopt Clay |
| --- | --- | --- |
| Current usability work | Reuse text entry, inputs, tables, popups, docking, and test hooks. Draw compact controls through the existing C shell. | Rebuild or integrate the equivalent editor interaction layer while changing layouts. |
| Visual control | Existing nodes and sidebar controls already use custom drawing. Theme tokens and deliberate layout are sufficient for the proposed changes. | Flexible declarative layout and render commands give direct control over UI composition. |
| Familiarity with web layouts | Patterns must be translated into ImGui layout and drawing calls. | Flexbox-like sizing and nested declarations resemble web layout, but HTML/CSS components do not run unchanged. |
| Input and editor mechanics | Already integrated into this application. Still requires accessibility and custom-control care. | Text editing/IME, selection, focus, keyboard navigation, docking, drag/drop, and accessibility need an explicit integration plan; a layout engine does not supply a complete editor toolkit. |
| In-game UI | Usable for development tools, less aligned with a custom shipping game interface. | Worth a separate experiment for HUD, menus, and other controlled layouts. |
| Migration risk | Small, independently reviewable refactor slices. | Two UI systems may disagree about focus, clipping, scaling, and input capture during a hybrid migration. |

Clay's primary documentation describes flexbox-like layout, scrolling, transitions, nested C declarations, and renderer-independent output. This is a useful resemblance to web layout, not compatibility with web component libraries. [Clay repository and documentation](https://github.com/nicbarker/clay)

Dear ImGui provides the editor controls and interaction model the project already uses, including customizable rendering and a docking branch. Keeping it avoids paying for a new toolkit before clarifying workflows. [Dear ImGui repository](https://github.com/ocornut/imgui)

Recommendation: keep ImGui through the editor refactor. Test Clay later on a bounded in-game UI such as a pause menu with settings. If editor layout still causes concrete problems, compare one real Inspector slice in both implementations. Require equivalent keyboard operation, Unicode/IME entry, clipping, scrolling, scale handling, renderer integration, and maintainable code before expanding Clay's role. “Easier for agents” should be measured by changes and defects in that slice, not inferred from syntax.

Share design tokens and examples across editor and game UI where useful. Avoid introducing a generic toolkit abstraction to support a migration that has not been chosen.

## Implementation sequence and acceptance

| Slice | Work | Acceptance |
| --- | --- | --- |
| 1. Selection, history, and action scope | Stable document/selection identities; per-document Undo; workspace Back/Forward; one runtime toolbar; explicit debug target. | Edit Player, navigate to Projectile and back, Undo the Player edit. Run Scene while opening graphs and selecting files; the target remains Scene. Both viewed panes reflect the same pause state. |
| 2. Shared controls and compact Inspector | Integrated search/clear, correct popup focus and result scrolling, compact property/vector rows, open component defaults, header Add/remove actions, consistent icons, no hint paragraphs. | Open add-node and type immediately; reach the last result by keyboard. X/Y values remain labeled at narrow widths. Add/remove component and Undo restore values. All actions work with keyboard focus. |
| 3. Asset routes and preview | Gameplay data document/Inspector, explicit save/apply behavior, consistent selection/open behavior, automatic still preview, bounded simulation loop. | Every file kind opens meaningfully or reports a specific failure. Projectile preview remains visible, can replay after death, and does not modify the authored scene or projectile. |
| 4. Trustworthy graph debugging | Reproduce the reported bubble sequence, bind instance identity explicitly, execution-only default activity, truthful idle/waiting/completed states. | One-shot Input animates only its executed path; untaken Branch stays quiet. Held input moves continuously. Switching between instances and nested functions shows the correct trace. Reduced motion remains informative. |
| 5. Graph authoring economy | Compact node families, vector/target editors, component field setters, Direction To evaluation, reusable graph functions. | Rewrite Health and projectile setup with fewer reconstruction nodes while preserving unrelated fields, runtime results, Undo, serialization, and type validation. |
| 6. Final craft pass | Pane proportions, initial framing, typography, icon optical size, separators, hover/focus behavior, scaling and empty/error states. | Compare the same startup, prototype, component, add-node, running graph, and narrow-window captures. No clipped controls or lost context at supported sizes. |

Slice 4 is a correctness track and should move ahead of later polish if the user's bubble failure is reproduced. The first complete demonstration should be the Player workflow through selection, editing, opening InputBehavior, and running Scene. That exercises the shared patterns before applying them everywhere.

Keep `editor/ui_shell.c` responsible for shared presentation and workspace controls. Introduce small focused modules only as behavior moves out of the current large files: property rows, asset navigation history, and runtime/debug presentation are useful boundaries. Leave gameplay mutation and ownership with the authoring/runtime code. Do not refactor the whole editor at once.

Use `make build` and `make test` for each meaningful implementation slice. Add tests for new behavior, especially history retention, popup keyboard routing, asset dispatch, preview resets, and graph trace identity. Visual acceptance still requires comparing fresh captures; passing the current tests alone cannot establish usability.

## Design references

The design judgment is specific to a dense desktop editor. Apple's material effects are not a requirement to add blur everywhere, and the Emil skill's web animation examples should not be transplanted literally into C rendering.

- [Apple components](https://developer.apple.com/design/human-interface-guidelines/components) provides the component taxonomy used to structure the review.
- [Apple toolbars](https://developer.apple.com/design/human-interface-guidelines/toolbars) supports deliberate grouping, recognizable controls, and clear command scope.
- [Apple sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars) supports familiar symbols, concise grouping, and keeping critical actions near the top.
- [Apple search fields](https://developer.apple.com/design/human-interface-guidelines/search-fields) describes a unified search field with a clear action, immediate results, and placement next to the content being filtered.
- [Apple split views](https://developer.apple.com/design/human-interface-guidelines/split-views) and [outline views](https://developer.apple.com/design/human-interface-guidelines/outline-views) inform pane sizing, persistent selection, and saved expansion state.
- Emil design engineering skill (local evidence: `SKILL.md`; not included in this repository) informs restraint, immediate repeated interactions, coherent components, and this Before/After review format.
- Apple design skill (local evidence: `SKILL.md`; not included in this repository) informs spatial consistency, control ownership, feedback, and reduced-motion behavior.

The existing [editor design record](../../editor-design.md) remains useful history. This review proposes the next refactor and corrects assumptions using the current implementation; it does not mark these recommendations as implemented.
