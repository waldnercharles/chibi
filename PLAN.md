Summary: Audit Studio authoring interactions, fix observed usability defects, and measure editor performance. Preserve the existing C/RmlUi Studio kit and compact visual system.

Context: Studio uses authored RML/RCSS, native kit controls, shared authoring documents, and a separate graph context. The requested design skills inform the audit; editor-specific contracts in docs/editor-ui-references.md and docs/prototypes/studio/DESIGN.md govern implementation. Design dials remain variance 2, motion 2, density 6. The downloaded Laws of UX reference guides decisions.

System impact: Document values and history remain in the authoring model. Preferences remain in studio_prefs, transient controls in the kit. Fixes should make cancellation lossless, saving predictable, selection visible across panes, and repeated rendering proportional to changed data.

Approach: Exercise each major interaction family, including cancellation, empty/error states, keyboard access, and recovery. Use existing regression fixtures and real browser input. Fix shared causes in kit/model boundaries rather than adding panel-specific workarounds. Apply Fitts's Law to targets, Jakob's Law to keyboard conventions, Proximity to grouping, and the Doherty Threshold to responsiveness. Keep a coverage ledger with measured results and explicit limits.

Changes implemented:
- src/editor/studio/studio_commands.c and studio_disk.c: save target consistency and unsaved reload protection.
- src/editor/studio/studio_graph*.c: connection gesture cancellation and undo.
- src/editor/studio/studio_designer*.c: selection synchronization and reveal.
- src/editor/studio/studio_assets.c: repeated asset filtering, sorting and reference counting.
- src/editor/rmlui/kit*.c and projects/studio/content/ui/: shared control states and rendered alignment defects found during QA.
- Shared property-label widths, pane-header truncation, and wrapping function signature fields preserve alignment at larger fonts and narrow pane widths.
- src/support/asset_text.c and asset_value.c: concise float serialization with exact f64 and native f32 round trips. Canonical KDL numeric spelling migrated across configured asset roots.

Verification:
- Build paired web applications with make web and verify rendering in the browser.
- Exercise shell, menus, palette, tabs, layouts, settings, assets, hierarchy, scene, graph, Properties, code, designer, playback and diagnostics. Cover light/dark, compact/comfortable, narrow/wide windows and keyboard interactions.
- Capture reproducible frame timing and editor counter measurements, before/after where an optimization is made.
- Run focused interaction regressions, make build, make test, and make check-format. Save screenshots, coverage and results with the audit.

Status: Complete. Native and paired web builds, the full test suite, focused rendered checks, formatting, and final browser verification passed after the alignment and numeric-serialization follow-up. Coverage, measurements, screenshots, and limits are recorded in docs/reviews/studio-ux-2026-10-02/README.md.
