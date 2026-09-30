# Inspector controls: Apple review

This pass updates the concept and the implementation contract. It does not change the native editor. It supersedes the earlier concept's Saved badges and Inspector status/Undo footer.

## Decisions

| Element | Before | Revised design and rationale |
| --- | --- | --- |
| Removed attachments | Saved badge on unchecked rows | Checkbox state only. Retaining authored settings is the default behavior; it does not need a badge on every row. |
| Edit feedback | Persistent footer with messages such as Edit undone | Remove the footer. Reflect changes directly in fields and attachment lists. Reveal affected content when Undo changes something offscreen. |
| Undo and Redo | Local text Undo button | Use the application's Edit menu and Command-Z / Shift-Command-Z. If dedicated controls prove necessary, use a paired set of toolbar icons with action-specific tooltips and disabled states. Back/Forward continues to mean navigation. |
| Search | Magnifier beside the focused input rectangle | One visible field containing magnifier, editable text, and trailing clear control. Focus highlights the field boundary; clear keeps search focus. |
| Multiselect | Searchable dropdown | Treat it as an anchored popover containing a search field and checkbox list. Keep it open for repeated choices; filtering must not change selection. This is a composition of controls, not the ordinary single-choice pop-up button. |
| Object identity | Player / Prototype text | A restrained box symbol beside the name/type pair. Use the same semantic prototype icon in the Navigator, breadcrumbs, and Inspector. Keep the text label. |

Apple describes search fields as combining a Search icon, Clear button, and placeholder text. AppKit's NSSearchField exposes search-button, cancel-button, and text bounds within the control. The prior concept's focus rectangle made the icon appear detached; the revised field includes it inside the input boundary. This is a correction to the concept, not evidence that the native editor has the same defect. [Search fields](https://developer.apple.com/design/human-interface-guidelines/search-fields), [NSSearchField](https://developer.apple.com/documentation/appkit/nssearchfield?language=objc).

Apple recommends dedicated Undo/Redo buttons only when necessary, putting them in a toolbar when provided. On macOS it specifies the Edit menu and standard shortcuts. Removing this footer follows that guidance without removing the underlying history. Menu labels should identify the pending action, such as Undo Remove Health, rather than merely announcing that an edit was undone afterward. [Undo and redo](https://developer.apple.com/design/human-interface-guidelines/undo-and-redo).

An anchored popover is appropriate for this small, temporary attachment-management task. Show one at a time, keep its relationship to Manage clear, dismiss with Escape/outside click, and restore keyboard focus to the trigger. Done dismisses immediately applied edits; it is not an Apply operation. The searchable checkbox combination is our product-specific design choice, not a claim that Apple supplies this exact picker. [Popovers](https://developer.apple.com/design/human-interface-guidelines/popovers).

The identity symbol communicates asset type, while Player identifies the particular asset. A simple monochrome shape and consistent optical weight fit Apple's interface-icon guidance. The concept uses a box glyph as an approximation; production should resolve a semantic prototype icon through the application's icon mapping. [Icons](https://developer.apple.com/design/human-interface-guidelines/icons).

## Reusable ImGui controls

There is already a shared layer in [editor/ui_shell.h](/Users/cwaldner/Personal/c-blueprints/editor/ui_shell.h:118) and [editor/ui_shell.c](/Users/cwaldner/Personal/c-blueprints/editor/ui_shell.c:640): icon buttons, search fields, disclosure rows, section headers, and a central theme. Extend it rather than starting a second styling system.

The current [search helper](/Users/cwaldner/Personal/c-blueprints/editor/ui_shell.c:717) already draws the magnifier inside the input, using padded text. Scene and Navigator callers still render their clear buttons separately. Consolidate the clear action into that helper and update those callers together. Focus, clear behavior, padding, and hit regions must remain consistent across all searches.

The next implementation should share these small controls:

- Search field: icon, placeholder, input, clear, focus handling, and standard dimensions.
- Attachment picker: anchored popup, search, bounded checkbox list, selected count, and dismissal. Components and Behaviors provide stable item IDs, labels, checked state, and receive toggle results. The caller owns filtering state and authored data; the control never deletes an asset.
- Property row and vector field: label alignment, widths, number formatting, and axis label outside the colored value rule.
- Inspector section: disclosure, optional trailing action, and consistent keyboard/hover affordances.
- Asset identity: semantic icon, primary name, secondary type, shared icon lookup.
- Icon button: consistent size, focus/pressed/disabled appearance, accessible name where supported, and tooltip. Reuse it for navigation and any future Undo/Redo pair.

Keep persistence, inheritance, retained attachment data, and undo transactions in the authoring layer. Shared UI functions should return edits through narrow C APIs. Avoid a generic widget framework or behavior callbacks where direct calls suffice. Extract control implementations out of ui_shell.c when needed to keep window layout separate from reusable controls; do not move code just to rename the layer.

## Verification for native implementation

Check mouse and keyboard operation at narrow Inspector widths and multiple display scales. Search clear must leave focus in the field; each checkbox toggle must retain focus and the filter. Reattachment must preserve authored settings without a Saved badge. Undo and Redo must restore attachment state through the existing document history. Native icon controls must be keyboard reachable; the current helper's custom InvisibleButton warrants checking, because its flags differ from the disclosure helper's explicit EnableNav.

The concept now omits the status footer, places both search accessories inside the field, adds the prototype icon, and supports attachment Undo/Redo shortcuts when a non-input control is focused. Text inputs retain their own editing behavior. This is not a full simulation of the application's menu and document-history routing.
