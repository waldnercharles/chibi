/* The command palette after cmdk: a modal list filtered and ranked with
 * command-score as the query changes, groups ordered by their best match, and
 * the matched characters in bold. Up, Down, Ctrl+N/P and Ctrl+J/K move the
 * selection; Enter chooses and Escape closes. */
#include "editor/rmlui/kit_internal.h"
#include "editor/ui/kit_icons.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rows kept per group while filtering. */
#define GROUP_LIMIT 8

/* ---------- command-score ---------- */

/* command-score (cmdk): continuous 1, word start 0.9, separator 0.8, jump
 * 0.17. The folded strings are lower case with '-' as ' '. */
typedef struct scorer
{
    const char *text, *query;
    char *folded, *abbr;
    size_t length, abbr_length;
    double *memo; /* (length + 1) * (abbr_length + 1) scores, -1 unknown */
} scorer;

static char *fold(const char *s)
{
    char *out = xstrdup(s);
    for (char *c = out; *c; c++) {
        *c = *c == '-' ? ' ' : (char)tolower((unsigned char)*c);
    }
    return out;
}

static bool gap(char c)
{
    return strchr("\\/_+.#\"@[({&", c) != NULL;
}

/* A match at index after a match ending at si: continuous, at a word start
 * or separator, or a jump that fades with its distance. */
static double weigh(const scorer *s, size_t si, size_t index, double score)
{
    if (index == si) {
        return score;
    }
    char before = s->folded[index - 1];
    if (before == ' ') {
        return score * 0.9;
    }
    if (gap(before)) {
        return score * 0.8;
    }
    score *= 0.17;
    return si > 0 ? score * pow(0.999, (double)(index - si)) : score;
}

/* The best score for abbr from ai matched in the text from si. */
static double score_from(const scorer *s, size_t si, size_t ai)
{
    double *memo = &s->memo[si * (s->abbr_length + 1) + ai];
    if (*memo >= 0) {
        return *memo;
    }
    if (ai == s->abbr_length) {
        return si == s->length ? 1.0 : 0.99;
    }
    double best = 0;
    char c = s->abbr[ai];
    for (char *at = strchr(s->folded + si, c); at; at = strchr(at + 1, c)) {
        size_t index = (size_t)(at - s->folded);
        double score = score_from(s, index + 1, ai + 1);
        if (score > best) {
            score = weigh(s, si, index, score);
            if (s->text[index] != s->query[ai]) {
                score *= 0.9999;
            }
        }
        best = math_max(best, score);
    }
    *memo = best;
    return best;
}

static double command_score(const char *text, const char *query)
{
    scorer s = {
        .text = text,
        .query = query,
        .folded = fold(text),
        .abbr = fold(query),
        .length = strlen(text),
        .abbr_length = strlen(query),
    };
    size_t cells = (s.length + 1) * (s.abbr_length + 1);
    s.memo = xmalloc(cells * sizeof *s.memo);
    for (size_t i = 0; i < cells; i++) {
        s.memo[i] = -1;
    }
    double score = score_from(&s, 0, 0);
    free(s.memo);
    free(s.abbr);
    free(s.folded);
    return score;
}

/* ---------- Ranking ---------- */

typedef struct command_row
{
    int index; /* into the items */
    int group; /* numbered by first appearance */
    double score;
    double group_score; /* the best score in its group */
} command_row;

static double item_score(const kit_command_item *item, const char *query)
{
    char *text = NULL;
    str_cat(&text, item->label);
    if (item->keywords) {
        str_catf(&text, " %s", item->keywords);
    }
    double score = command_score(text, query);
    free(text);
    return score;
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

/* The rows to show in order. A query filters unranked items by
 * command-score, orders groups by their best match and keeps each group's
 * best GROUP_LIMIT. Otherwise scores fall with item order, so groups keep
 * their first appearance and rows their order. */
static command_row *rank_rows(const kit_command_item *items, int count, bool ranked, const char *query)
{
    bool filter = !ranked && query[0];
    double threshold = strlen(query) > 1 ? 0.02 : 0;
    command_row *rows = NULL;
    for (int i = 0; i < count; i++) {
        double score = ranked ? count - i : 1;
        if (filter) {
            score = item_score(&items[i], query);
        }
        if (filter && score <= threshold) {
            continue;
        }
        apush(rows, (command_row){ .index = i, .score = score });
    }
    if (!acount(rows)) {
        return rows;
    }
    group_rows(rows, items);
    qsort(rows, (size_t)acount(rows), sizeof *rows, compare_rows);
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
        rml_element_set_value(c->input, "");
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

/* The label with the query's characters in bold when they all appear in
 * order, else the plain label. */
static char *match_rml(const char *label, const char *query)
{
    char *rml = NULL;
    size_t matched = 0, length = strlen(query);
    for (const char *ch = label; *ch; ch++) {
        bool hit = matched < length && same_letter(*ch, query[matched]);
        if (hit) {
            str_cat(&rml, "<b>");
        }
        escape(&rml, (char[]){ *ch, 0 });
        if (hit) {
            str_cat(&rml, "</b>");
            matched++;
        }
    }
    if (matched == length) {
        return rml;
    }
    free(rml);
    rml = NULL;
    escape(&rml, label);
    return rml;
}

static void emit_row(kit *k, const kit_command_item *it, int index, bool selected)
{
    command_state *c = &k->command;
    char key[32];
    snprintf(key, sizeof key, "i%d", index);
    node *item = emit(k, key, KIND_COMMAND_ITEM, "div", "cmd-item");
    widget *w = &item->w;
    if (!w->label_el) {
        w->icon_el = raw_icon(item->el, "", 0);
        w->label_el = raw(item->el, "span", "label", NULL);
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
    char *rml = match_rml(it->label ? it->label : "", kit_command_query(k));
    if (str_set(&w->label, rml)) {
        rml_element_set_inner_rml(w->label_el, w->label);
    }
    free(rml);
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

static void emit_rows(kit *k, const kit_command_item *items, const command_row *rows)
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
        emit_row(k, it, rows[r].index, r == k->command.selected);
    }
    if (!acount(rows)) {
        node *empty = emit(k, "@empty", KIND_BOX, "div", "cmd-empty");
        const char *query = kit_command_query(k);
        set_text(empty, frame_format(k, "No results for \"%s\"", query));
    }
}

static int command(kit *k, const kit_command_item *items, int count, bool ranked)
{
    command_state *c = &k->command;
    if (!c->open) {
        return -1;
    }
    node *n = emit_palette(k);
    command_row *rows = rank_rows(items, count, ranked, kit_command_query(k));
    aclear(c->shown);
    for (int r = 0; r < acount(rows); r++) {
        apush(c->shown, rows[r].index);
    }
    c->selected = math_clamp(c->selected, 0, math_max(0, acount(c->shown) - 1));
    push_node(k, n);
    emit_rows(k, items, rows);
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
    return command(k, items, count, false);
}

int kit_command_ranked(kit *k, const kit_command_item *items, int count)
{
    return command(k, items, count, true);
}
