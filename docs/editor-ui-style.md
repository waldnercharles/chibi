# Editor UI style

This document defines the visual baseline for the native editor. The HTML mockups are the product reference. Dear ImGui is the rendering substrate, not the visual style.

## Direction

Use a dark workstation interface with the spatial model of Unreal and Unity, the restraint of VS Code, and the component discipline of Fluent 2, Carbon, and Radix Colors.

The editor should read in this order:

1. active context;
2. primary object or asset;
3. available actions;
4. supporting metadata;
5. diagnostics.

The interface may keep linked selections in separate categories. One item is the active editing context. Secondary selections use a weaker surface and never compete with the active item.

## Baseline tokens

These values are starting points, not rules that prevent iteration.

| Token | Baseline |
| --- | --- |
| body text | 13 px |
| item name | 14 px |
| section label | 14 px, muted, semibold |
| metadata | 11 px, muted |
| spacing | 4, 8, 12, 16, 24 px |
| row height | 42 to 46 px |
| control height | 28 to 32 px |
| corner radius | 5 to 7 px |
| border | 1 px |
| hover transition | 120 ms |
| panel transition | 180 ms |

Frequent list rows should show a name, an icon, and one state signal. Counts belong in an inspector, tooltip, or dedicated context view unless they are needed for a decision.

## Color roles

Use semantic roles instead of choosing colors per widget:

- canvas and shell background;
- panel and raised panel;
- hover and active selection;
- border and quiet border;
- primary and muted text;
- accent blue for focus and navigation;
- purple for prototypes;
- blue for behavior graphs;
- green for healthy runtime or saved state;
- amber for modified state;
- red for errors and destructive actions.

Dark surfaces should get slightly lighter as they rise. Selection should use a surface plus a border, not color alone. Every state needs a non-color signal such as an icon, outline, or text label.

## Interaction rules

- A plus button opens creation and duplication actions for the nearest section.
- Destructive or low-frequency actions live in menus or context menus.
- New assets receive a name, become the active context, and open in the appropriate editor.
- A prototype and a behavior asset may both remain selected because they are linked context. Only one receives the active outline.
- Tooltips explain icon-only controls. Buttons with text do not need redundant tooltips.
- Selection changes are immediate. Motion is reserved for focus, menus, and panel transitions.
- Do not put implementation state into a dense label. Draw rows from separate name, metadata, state, and icon fields.

## ImGui editor references

The requested repositories were cloned locally under `/tmp/cblueprints-ui-references` for study. They remain outside the project and are not compiled or copied into the application.

- `wpsimon09/Vulkan-RTX` at `33bbe86`: `Internal/Editor/UIContext/UIContext.cpp` merges Font Awesome into a 16 px editor font and defines a complete dark palette. `External/ImGuiSpectrum` provides a reusable palette. `Internal/Editor/Views/AssetsBrowser/AssetsBrowserDrawUtils.hpp` uses an `InvisibleButton`, a fixed tile rectangle, and the measured icon size to center custom asset icons.
- `GloriousPtr/ArcGameEngine` at `4a091e2`: `Arc-Editor/src/Utils/EditorTheme.*` separates regular, small, and bold fonts, merges Material Design icons with explicit glyph metrics, and centralizes spacing, rounding, popup, and semantic colors. `BasePanel.cpp` applies consistent panel padding, while `AssetPanel.cpp` uses icon-led context menus and selected header colors.

These examples support the current direction: keep a small project theme layer, use a real heading font when available, measure icon glyphs in the font and size being drawn, and make custom list rows from invisible hit targets plus draw-list geometry.

## References

- Fluent 2 design tokens: <https://fluent2.microsoft.design/design-tokens>
- Fluent 2 typography: <https://fluent2.microsoft.design/typography>
- Fluent 2 motion: <https://fluent2.microsoft.design/motion>
- Carbon color: <https://carbondesignsystem.com/elements/color/overview/>
- Carbon typography: <https://carbondesignsystem.com/elements/typography/type-sets/>
- Carbon motion: <https://carbondesignsystem.com/elements/motion/overview/>
- Radix Colors scales: <https://www.radix-ui.com/colors/docs/palette-composition/scales>
- Material 3: <https://m3.material.io/>

The implementation starts with the reusable row and section primitives in `editor/ui_shell.c`. New panes should use those primitives instead of adding raw ImGui labels and default widgets directly.
