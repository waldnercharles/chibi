#ifndef CHIBI_CODE_CONTROL_H
#define CHIBI_CODE_CONTROL_H

#include "ui/rml.h"

/* Generic document view, with no document identity or history of its own.
 * <code-editor value selection-start selection-end selection-caret readonly disabled wrap highlights
 *   completion-open completion-list request-command request-sequence/>
 * Selection attributes and emitted ranges use UTF-8 byte offsets. `highlights`
 * is a JSON array of {start,end,classes} (or kind), sorted, disjoint byte ranges. Classes
 * style authored token spans; text is always inserted as text, never markup.
 *
 * code-input(value,start,end,caret,line,column): an edit, including an IME commit or paste.
 * code-selection(start,end,caret,line,column): a changed selection.
 * code-command(command,start,end,caret): undo, redo, save, find, definition,
 * references, suggest, cancel. completion-open also routes Up/Down/Enter/Tab
 * as completion-up/completion-down/completion-accept. completion-list names an
 * authored listbox selector: its selected option's aria-posinset keeps that
 * option visible when keyboard selection changes. The app applies the resulting document value.
 * code-commit: blur ends the app's current edit transaction.
 * Native textarea owns UTF-8 input, mouse selection, IME and clipboard. Tab
 * inserts a tab. All document/history/completion policy belongs to the app.
 * Structure: .code-input textarea, .code-highlight text overlay. Authored RCSS
 * supplies typography, selection and token colors; `caret-color` should be
 * explicit when highlights make the native text transparent. No Vim controller
 * is provided by this primitive. Set request-command to copy/cut/paste/selectAll,
 * then change request-sequence to invoke it using the retained selection. */
void code_control_register();

/* Offset conversion for RmlUi's Unicode character-based selection API. An
 * offset inside a UTF-8 sequence rounds down; negative/oversized offsets clamp.
 * Input must be valid UTF-8 (the document service and RmlUi enforce this). */
int code_text_byte_offset(const char *text, int characters);
int code_text_character_offset(const char *text, int bytes);
/* One-based logical Unicode code-point line/column at the byte offset. */
void code_text_position(const char *text, int bytes, int *line, int *column);
/* Owned replacement, UTF-8 range rounded to boundaries. */
char *code_text_replace(const char *text, int start, int end, const char *replacement);
/* NULL means the native textarea handles the key. */
const char *code_text_command(rml_key key, unsigned modifiers, bool mac);

#endif
