/* The command palette after cmdk: a modal list filtered and ranked as the
 * query changes, by the search Studio's other lists and the MCP share
 * (support/text_search.h), groups ordered by their best match, and the matched
 * words in bold, over an item's description. Up, Down, Ctrl+N/P and Ctrl+J/K
 * move the selection; Enter chooses and Escape closes. */
#include "editor/rmlui/kit_internal.h"
#include "editor/ui/kit_icons.h"
#include "support/math.h"
#include "support/text_search.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rows kept per group while filtering. */
#define GROUP_LIMIT 8

/* ---------- Ranking ---------- */

typedef struct command_row
{
    int index; /* into the items */
    int group; /* numbered by first appearance */
    double score;
    double group_score; /* the best score in its group */
} command_row;

/* -1 when the item does not match. */
static double item_score(const kit_command_item *item, const char *query)
{
    int score = text_search_score(query, item->label, item->keywords);
    return score < 0 ? -1 : score + item->boost;
}

/* Numbers the rows' groups and gives each row its group's best score. */
static void group_rows(command_row *rows, const kit_command_item *items)
{
    const char **names = NULL;
    double *best = NULL;
    for (int i = 0; i < acount(rows); i++) {
        const char *name = items[rows[i].index].group;
        int g = 0;
        while (g < acount(names) && !str_eq(names[g], name)) {
            g++;
        }
        if (g == acount(names)) {
            apush(names, name);
            apush(best, rows[i].score);
        }
        rows[i].group = g;
        best[g] = math_max(best[g], rows[i].score);
    }
    for (int i = 0; i < acount(rows); i++) {
        rows[i].group_score = best[rows[i].group];
    }
    afree(names);
    afree(best);
}

/* Groups by best score, then by first appearance; rows by score, then by
 * item order. std::stable_sort's order, with the index as its tiebreak. */
static int compare_rows(const void *a, const void *b)
{
    const command_row *x = a, *y = b;
    if (x->group_score != y->group_score) {
        return x->group_score > y->group_score ? -1 : 1;
    }
    if (x->group != y->group) {
        return x->group - y->group;
    }
    if (x->score != y->score) {
        return x->score > y->score ? -1 : 1;
    }
    return x->index - y->index;
}

static void limit_groups(command_row *rows)
{
    int kept = 0, run = 0, group = -1;
    for (int i = 0; i < acount(rows); i++) {
        run = rows[i].group == group ? run + 1 : 0;
        group = rows[i].group;
        if (run < GROUP_LIMIT) {
            rows[kept++] = rows[i];
        }
    }
    asetlen(rows, kept);
}

/* The rows to show in order. A query filters unranked items, orders groups
 * by their best match and keeps each group's best GROUP_LIMIT. Otherwise
 * scores fall with item order, so groups keep their first appearance and
 * rows their order. */
static command_row *rank_rows(const kit_command_item *items, int count, bool ranked, const char *query)
{
    bool filter = !ranked && query[strspn(query, " ")];
    command_row *rows = NULL;
    for (int i = 0; i < count; i++) {
        double score = ranked ? count - i : 1;
        if (filter) {
            score = item_score(&items[i], query);
        }
        if (filter && score < 0) {
            continue;
        }
        apush(rows, (command_row){ .index = i, .score = score });
    }
    if (!acount(rows)) {
        return rows;
    }
    group_rows(rows, items);
    asort(rows, compare_rows);
    if (filter) {
        limit_groups(rows);
    }
    return rows;
}

/* ---------- Events ---------- */

void command_press(kit *k, rml_element *target)
{
    if (k->command.open && k->command_backdrop && target == k->command_backdrop) {
        k->command.open = false;
    }
}

/* Applies a key to the open palette; false when the key is not its own. */
static bool apply_command_key(command_state *c, rml_key key, bool ctrl)
{
    int count = acount(c->shown);
    if (key == RML_KEY_ESCAPE) {
        c->open = false;
        return true;
    }
    if (key == RML_KEY_DOWN || (ctrl && (key == RML_KEY_N || key == RML_KEY_J))) {
        c->selected = math_min(c->selected + 1, count - 1);
        c->reveal = true;
        return true;
    }
    if (key == RML_KEY_UP || (ctrl && (key == RML_KEY_P || key == RML_KEY_K))) {
        c->selected = math_max(c->selected - 1, 0);
        c->reveal = true;
        return true;
    }
    if (key == RML_KEY_HOME || key == RML_KEY_END) {
        c->selected = key == RML_KEY_HOME ? 0 : math_max(count - 1, 0);
        c->reveal = true;
        return true;
    }
    if (key == RML_KEY_RETURN || key == RML_KEY_NUMPADENTER) {
        if (c->selected >= 0 && c->selected < count) {
            c->chosen = c->shown[c->selected];
        }
        return true;
    }
    return false;
}

bool command_key(kit *k, const rml_event *e)
{
    if (!k->command.open) {
        return false;
    }
    bool ctrl = event_modifiers(e) & KIT_MOD_CTRL;
    if (apply_command_key(&k->command, e->key, ctrl)) {
        rml_event_stop_propagation(e);
    }
    return true;
}

bool command_change(kit *k, rml_element *target)
{
    command_state *c = &k->command;
    if (!c->open || target != c->input) {
        return false;
    }
    free(c->query);
    c->query = element_value(target);
    c->selected = 0;
    c->reveal = true;
    return true;
}

void command_click(kit *k, node *n)
{
    k->command.chosen = n->w.pending_index;
}

void command_hover(kit *k, node *n)
{
    command_state *c = &k->command;
    for (int i = 0; i < acount(c->shown); i++) {
        if (c->shown[i] == n->w.pending_index) {
            c->selected = i;
            return;
        }
    }
}

/* ---------- Command palette ---------- */

void kit_command_open(kit *k, const char *placeholder)
{
    close_all_popups(k);
    command_state *c = &k->command;
    free(c->placeholder);
    free(c->query);
    afree(c->shown);
    *c = (command_state){ .open = true, .focus = true, .chosen = -1 };
    str_set(&c->placeholder, placeholder);
}

bool kit_command_is_open(const kit *k)
{
    return k->command.open;
}

const char *kit_command_query(const kit *k)
{
    return k->command.query ? k->command.query : "";
}

void kit_command_set_query(kit *k, const char *query)
{
    command_state *c = &k->command;
    str_set(&c->query, query);
    c->selected = 0;
    c->reveal = true;
    if (c->input) {
        rml_element_set_value(c->input, query);
        int end = (int)strlen(query);
        rml_element_set_selection_range(c->input, end, end);
    }
}

static void build_palette(node *n)
{
    rml_element *row = raw(n->el, "div", "cmd-input", NULL);
    raw_icon(row, "", KIT_ICON_SEARCH);
    n->w.input_el = rml_element_create_child(row, "input", NULL);
    rml_element_set_attribute(n->w.input_el, "type", "text");
    rml_element *list = raw(n->el, "div", "cmd-list", NULL);
    rml_element *footer = raw(n->el, "div", "cmd-footer", NULL);
    raw(footer, "span", "kbd-key", "\xe2\x86\x91");
    raw(footer, "span", "kbd-key", "\xe2\x86\x93");
    raw(footer, "span", "hint", "Navigate");
    raw(footer, "span", "kbd-key", "\xe2\x86\xb5");
    raw(footer, "span", "hint", "Open");
    raw(footer, "span", "spacer", NULL);
    raw(footer, "span", "kbd-key", "esc");
    raw(footer, "span", "hint", "Close");
    n->container = list;
}

/* The backdrop and the dialog, whose input keeps the focus. */
static node *emit_palette(kit *k)
{
    command_state *c = &k->command;
    node *backdrop = emit_in(k, k->layer, "@command-backdrop", KIND_BOX, "div", "command-backdrop");
    presence(k, backdrop);
    k->command_backdrop = backdrop->el;
    node *n = emit_in(k, k->layer, "@command", KIND_COMMAND, "div", "command");
    presence(k, n);
    if (!n->w.input_el) {
        build_palette(n);
    }
    c->input = n->w.input_el;
    rml_element_set_attribute(c->input, "placeholder", c->placeholder);
    if (c->focus) {
        c->focus = false;
        rml_element_set_value(c->input, kit_command_query(k));
    }
    /* The dialog is modal: focus stays in its input (cmdk). A focus request
       made while the input was being created can be refused, which dropped the
       first typed characters. */
    if (rml_context_focus_element(k->context) != c->input) {
        rml_element_focus(c->input, true);
    }
    return n;
}

static bool same_letter(char a, char b)
{
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static bool word_start(const char *label, size_t i)
{
    return !i || !isalnum((unsigned char)label[i - 1]) ||
           (isupper((unsigned char)label[i]) && islower((unsigned char)label[i - 1]));
}

/* Where `word` (`length` bytes) is in the label, at a word start if it can
   be, or -1. */
static long find_word(const char *label, const char *word, size_t length)
{
    long anywhere = -1;
    for (size_t i = 0; label[i]; i++) {
        size_t n = 0;
        while (n < length && label[i + n] && same_letter(label[i + n], word[n])) {
            n++;
        }
        if (n < length) {
            continue;
        }
        if (word_start(label, i)) {
            return (long)i;
        }
        anywhere = anywhere < 0 ? (long)i : anywhere;
    }
    return anywhere;
}

/* Marks each query word where the label holds it, or else its letters in
   order; false when some word is in neither form. */
static bool mark_words(const char *label, const char *query, bool *hits)
{
    for (const char *word = query; *word;) {
        size_t length = strcspn(word, " ");
        long at = length ? find_word(label, word, length) : 0;
        for (size_t i = 0; length && at >= 0 && i < length; i++) {
            hits[at + (long)i] = true;
        }
        for (size_t i = 0, matched = 0; length && at < 0 && label[i]; i++) {
            bool hit = matched < length && same_letter(label[i], word[matched]);
            hits[i] |= hit;
            matched += hit;
            at = matched == length ? 0 : at;
        }
        if (length && at < 0) {
            return false;
        }
        word += length;
        word += strspn(word, " ");
    }
    return true;
}

/* Appends label[start, end). RmlUi drops a text node of only whitespace, so
 * such a run keeps its spaces as no-break spaces. */
static void match_run(char **rml, const char *label, size_t start, size_t end)
{
    bool blank = true;
    for (size_t i = start; i < end; i++) {
        blank &= label[i] == ' ';
    }
    for (size_t i = start; i < end; i++) {
        if (blank) {
            str_cat(rml, "\xc2\xa0");
            continue;
        }
        escape(rml, (char[]){ label[i], 0 });
    }
}

/* The label with the query's words in bold where it holds them, or their
   letters in order, else the plain label, as when only keywords matched.
   Consecutive matches share one <b>. */
static char *match_rml(const char *label, const char *query)
{
    size_t count = strlen(label);
    bool *hits = xcalloc(count + 1, sizeof *hits);
    char *rml = NULL;
    if (!mark_words(label, query, hits)) {
        free(hits);
        escape(&rml, label);
        return rml;
    }
    /* A space between two matches joins them in one run. */
    for (size_t i = 1; i + 1 < count; i++) {
        hits[i] |= label[i] == ' ' && hits[i - 1] && hits[i + 1];
    }
    size_t start = 0;
    while (start < count) {
        size_t end = start;
        while (end < count && hits[end] == hits[start]) {
            end++;
        }
        str_cat(&rml, hits[start] ? "<b>" : "");
        match_run(&rml, label, start, end);
        str_cat(&rml, hits[start] ? "</b>" : "");
        start = end;
    }
    free(hits);
    return rml;
}

static void emit_row(kit *k, const kit_command_item *it, int index, bool selected, const char *query)
{
    command_state *c = &k->command;
    char key[32];
    snprintf(key, sizeof key, "i%d", index);
    node *item = emit(k, key, KIND_COMMAND_ITEM, "div", "cmd-item");
    widget *w = &item->w;
    if (!w->label_el) {
        w->icon_el = raw_icon(item->el, "", 0);
        w->head_el = raw(item->el, "div", "text", NULL);
        w->label_el = raw(w->head_el, "span", "label", NULL);
        w->about_el = raw(w->head_el, "span", "about", NULL);
        w->meta_el = raw(item->el, "span", "meta", NULL);
        w->kbd_el = raw(item->el, "span", "kbd", NULL);
    }
    w->pending_index = index;
    if (w->icon != it->icon) {
        w->icon = it->icon;
        char text[5];
        utf8(text, it->icon);
        rml_element_set_inner_rml(w->icon_el, text);
    }
    char *rml = match_rml(it->label ? it->label : "", query);
    if (str_set(&w->label, rml)) {
        rml_element_set_inner_rml(w->label_el, w->label);
    }
    free(rml);
    set_element_text(w->about_el, it->description);
    set_flag(item, "data-described", it->description && it->description[0]);
    set_element_text(w->meta_el, it->meta);
    set_element_text(w->kbd_el, it->shortcut);
    /* Keyboard moves reveal the selection with the least scrolling; hovering
       selects without scrolling (cmdk). */
    if (selected && c->reveal) {
        rml_element_scroll_into_view(item->el, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        c->reveal = false;
    }
    set_flag(item, "data-selected", selected);
}

static void emit_rows(kit *k, const kit_command_item *items, const command_row *rows, const char *query)
{
    int group = -1;
    for (int r = 0; r < acount(rows); r++) {
        const kit_command_item *it = &items[rows[r].index];
        if (rows[r].group != group && !str_eq(it->group, "")) {
            const char *key = frame_format(k, "g:%s", it->group);
            node *heading = emit(k, key, KIND_BOX, "div", "cmd-group");
            set_text(heading, it->group);
        }
        group = rows[r].group;
        emit_row(k, it, rows[r].index, r == k->command.selected, query);
    }
    if (!acount(rows)) {
        node *empty = emit(k, "@empty", KIND_BOX, "div", "cmd-empty");
        const char *query = kit_command_query(k);
        set_text(empty, frame_format(k, "No results for \"%s\"", query));
    }
}

static int command(kit *k, const kit_command_item *items, int count, bool ranked, const char *query)
{
    command_state *c = &k->command;
    if (!c->open) {
        return -1;
    }
    node *n = emit_palette(k);
    command_row *rows = rank_rows(items, count, ranked, query);
    aclear(c->shown);
    for (int r = 0; r < acount(rows); r++) {
        apush(c->shown, rows[r].index);
    }
    c->selected = math_clamp(c->selected, 0, math_max(0, acount(c->shown) - 1));
    push_node(k, n);
    emit_rows(k, items, rows, query);
    pop_node(k);
    afree(rows);
    int chosen = c->chosen;
    if (chosen >= 0) {
        c->open = false;
        rml_element_blur(c->input);
    }
    c->chosen = -1;
    return chosen;
}

int kit_command(kit *k, const kit_command_item *items, int count)
{
    return command(k, items, count, false, kit_command_query(k));
}

int kit_command_filtered(kit *k, const kit_command_item *items, int count, const char *query)
{
    return command(k, items, count, false, query);
}

int kit_command_ranked(kit *k, const kit_command_item *items, int count)
{
    return command(k, items, count, true, kit_command_query(k));
}
