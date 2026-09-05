# Editor design and implementation plan

## Direction agreed with the user

Build a quiet, document-centered C23 ImGui editor for our own authoring needs.
Synapse influences layout and visual presentation, not the engine architecture.
Use independently written code and our own assets; its AGPL source is not a
source-port dependency. Keep existing graph authoring, reflection, Prototypes,
systems, scenes, and preview capabilities throughout the transition.

The central workspace is not a single exclusive view. Documents remain dockable
and splittable: an event graph and the scene must be usable side by side. Opening
or focusing a document must not destroy the user's arrangement.

## Design references

Loaded the user-supplied
[apple-design skill](https://raw.githubusercontent.com/emilkowalski/skills/refs/heads/main/skills/apple-design/SKILL.md)
directly after it was identified outside the active skill catalog. Also reviewed
the game-ui-ux layout guidance, Cute Framework integration skill, and Apple's
published [Layout](https://developer.apple.com/design/human-interface-guidelines/layout)
and [Sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars)
guidance (including Apple's documentation JSON).

Apply grouping, alignment, sufficient space for essential content, progressive
disclosure, and discoverable sidebar visibility controls. Keep the navigator
discoverable on first launch. Avoid copying platform materials or introducing
decorative animation before the basic interactions work well.

Translate the skill's web-oriented examples into native ImGui behavior: instant
press feedback with activation on release, precise pointer-following drags and
splitters, predictable restoration, and no input lockouts during transitions.
Keep common controls discoverable rather than hiding everything to appear minimal.
Use consistent typography and spacing to establish hierarchy. Any later motion
must be interruptible and offer a reduced-motion alternative; springs, momentum,
and translucency are not requirements for precision editor controls.

## Workspace

- Navigator on the left: assets and scene contents, searchable and grouped.
- Documents in the center: freely dockable and splittable. Provide a discoverable
  graph-and-scene arrangement in addition to manual tab dragging.
- Inspector on the right: one place for properties of the active selection.
  Document context follows deliberate focus/selection, not mouse hover. A visible
  scene must not steal graph selection just because it renders another frame.
- Diagnostics on demand: show actionable errors and a compact indication when
  hidden; do not permanently spend a quarter of the window on internal state.
- Related controls next to their content. Advanced runtime details are optional.

Every auxiliary pane needs a visible close/hide affordance and a checked View
menu entry to reopen it. Focus mode must restore the previous visibility choices.
Reset Layout is an explicit recovery command, never a side effect of switching
documents. Preserve ImGui docking persistence; separately define persistence for
any new visibility preferences before claiming they survive restart.

## Editing and preview contract

Working edits, disk saves, and the last valid running definition are distinct.
The intended default is automatic preview of valid edits, at a safe frame
boundary, without requiring Save. Incomplete or invalid graphs keep the previous
valid version running and report the error at its source. State-incompatible
edits require an explicit Restart rather than silently resetting the world.

Scene and isolated preview use the same editing commands. Preview controls and
input must identify their target; typing in a graph must not control the scene.
Shader and particle graphs are future document types, not existing capabilities
or requirements to add generic plugin infrastructure now.

## Ordered implementation

1. **Pane usability and simultaneous views.** Quiet defaults; close/reopen panes;
   restore layout; a graph-and-scene split; retain manual docking. Exercise actual
   ImGui interactions and assert both central views have usable, disjoint areas.
2. **Context and consolidation.** Consolidate related properties and document
   controls, remove redundant status displays, and make inspector ownership
   explicit when multiple documents are visible. Preserve document edits and
   selection while moving focus between graph and scene.
3. **Immediate behavior feedback.** Edit a behavior used by a scene entity, see
   the changed behavior without saving, reject a broken graph without replacing
   the valid runtime, recover after fixing it, and distinguish restart-required
   changes. Cover these outcomes through shared commands and native widgets.
4. **Refine the presentation.** Consistent spacing, restrained accent colors,
   readable labels, keyboard focus, useful empty states, and narrow-window
   behavior. Inspect the real rendered editor, not only model tests.
5. **Architecture follow-through.** Assess the paused architecture experiment
   against the working workflow before integrating anything. DLL loading,
   remodule, and additional document types remain separate milestones.

## Verification

Run `make test` with sanitizers, warnings, and ccache; retain graph/model regression
coverage and the native resize scenario. Add behavioral UI assertions for changed
interactions rather than tests that duplicate visibility implementation. Verify
hide/reopen does not lose document state and that switching document focus does
not collapse a custom split. Formatting is required. Distinguish tested behavior
from remaining design intent when reporting progress.

## First slice delivered

Integrated pane close/hide/reopen controls and a checked View menu. Diagnostics
start hidden; graph compilation with warnings or errors reveals Compiler Results.
New/reset layouts place Scene and Node Canvas side by side. The explicit
`Reset Workspace: Graph + Scene Side by Side` preset rebuilds the entire dock
layout while retaining pane visibility choices except for reopening both central
views. Manual docking remains available and is not reset on document changes.
Pane visibility currently lasts for the process; it is not saved across launches.

`make test` passed in the main working tree with sanitizers, warnings, and ccache,
including native Content hide/reopen interactions, simultaneous pane geometry,
hidden-Scene focus handling, and the resize scenario. The preset test invokes the
shared action after hiding both views; it does not yet drive a manual re-docking
gesture or the title-bar close button. Formatting passed in the implementation
worktree. The screenshot command returned a black canvas due to the existing
capture path omitting the screen-rendered ImGui layer, so visual polish has not
been accepted from that capture.

Context consolidation, focus mode, persistent visibility preferences, immediate
behavior preview, shader/particle graphs, and the broader architecture changes
remain future work. This slice improves workspace controls; it does not complete
the document-centered redesign.
