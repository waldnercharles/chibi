#include "support/xalloc.h"
#include "ui/controls/code_control.h"

#include <string.h>

static bool continuation(unsigned char c)
{
    return (c & 0xc0) == 0x80;
}

int code_text_byte_offset(const char *text, int characters)
{
    int bytes = 0;
    for (int at = 0; text[bytes] && at < characters; at++) {
        bytes++;
        while (continuation((unsigned char)text[bytes])) {
            bytes++;
        }
    }
    return bytes;
}

int code_text_character_offset(const char *text, int bytes)
{
    int at = 0, characters = 0;
    while (text[at] && at < bytes) {
        int next = at + 1;
        while (continuation((unsigned char)text[next])) {
            next++;
        }
        if (next > bytes) {
            break;
        }
        at = next;
        characters++;
    }
    return characters;
}

void code_text_position(const char *text, int bytes, int *line, int *column)
{
    int end = code_text_byte_offset(text, code_text_character_offset(text, bytes));
    *line = *column = 1;
    for (int at = 0; at < end; at++) {
        if (text[at] == '\n') {
            (*line)++;
            *column = 1;
        } else if (!(text[at] == '\r' && text[at + 1] == '\n') && !continuation((unsigned char)text[at])) {
            (*column)++;
        }
    }
}

char *code_text_replace(const char *text, int start, int end, const char *replacement)
{
    start = code_text_byte_offset(text, code_text_character_offset(text, start));
    end = code_text_byte_offset(text, code_text_character_offset(text, end));
    if (end < start) {
        int swap = start;
        start = end;
        end = swap;
    }
    size_t length = strlen(text), inserted = strlen(replacement);
    char *result = xmalloc(length - (size_t)(end - start) + inserted + 1);
    memcpy(result, text, (size_t)start);
    memcpy(result + start, replacement, inserted);
    memcpy(result + start + inserted, text + end, length - (size_t)end + 1);
    return result;
}

const char *code_text_command(rml_key key, unsigned modifiers, bool mac)
{
    bool primary = (modifiers & (mac ? RML_KM_META : RML_KM_CTRL)) != 0;
    bool shift = (modifiers & RML_KM_SHIFT) != 0;
    if (primary && !(modifiers & RML_KM_ALT)) {
        switch (key) {
            case RML_KEY_Z: return shift ? "redo" : "undo";
            case RML_KEY_Y: return "redo";
            case RML_KEY_S: return "save";
            case RML_KEY_F: return "find";
            default: break;
        }
    }
    if (key == RML_KEY_F12) {
        return shift ? "references" : "definition";
    }
    if (key == RML_KEY_SPACE && (modifiers & RML_KM_CTRL)) {
        return "suggest";
    }
    return key == RML_KEY_ESCAPE ? "cancel" : NULL;
}
