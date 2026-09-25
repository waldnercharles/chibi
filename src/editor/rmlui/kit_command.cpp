/* The command palette after cmdk: a modal list filtered and ranked with
 * command-score as the query changes, groups ordered by their best match, and the
 * matched characters in bold. Up, Down, Ctrl+N/P and Ctrl+J/K move the
 * selection; Enter chooses and Escape closes. */
#include "editor/rmlui/kit_internal.hpp"

#include "editor/ui/kit_icons.h"

#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kit_impl {

/* command-score (cmdk): continuous 1, word start 0.9, separator 0.8, jump 0.17. */
static double command_score(const std::string &string, const std::string &query)
{
    std::string lower, abbr;
    for (char c : string) {
        lower += (c == '-' ? ' ' : (char)std::tolower((unsigned char)c));
    }
    for (char c : query) {
        abbr += (c == '-' ? ' ' : (char)std::tolower((unsigned char)c));
    }
    std::map<std::pair<size_t, size_t>, double> memo;
    auto gap = [](char c) { return std::strchr("\\/_+.#\"@[({&", c) != nullptr; };
    std::function<double(size_t, size_t)> score = [&](size_t si, size_t ai) -> double {
        auto key = std::make_pair(si, ai);
        auto found = memo.find(key);
        if (found != memo.end()) {
            return found->second;
        }
        if (ai == abbr.size()) {
            return si == lower.size() ? 1.0 : 0.99;
        }
        double best = 0;
        size_t index = lower.find(abbr[ai], si);
        while (index != std::string::npos) {
            double s = score(index + 1, ai + 1);
            if (s > best) {
                if (index == si) {
                } else if (lower[index - 1] == ' ') {
                    s *= 0.9;
                } else if (gap(lower[index - 1])) {
                    s *= 0.8;
                } else {
                    s *= 0.17;
                    if (si > 0) {
                        s *= std::pow(0.999, (double)(index - si));
                    }
                }
                if (string[index] != query[ai]) {
                    s *= 0.9999;
                }
            }
            best = std::max(best, s);
            index = lower.find(abbr[ai], index + 1);
        }
        memo[key] = best;
        return best;
    };
    return score(0, 0);
}

void command_press(kit *k, Rml::Element *target)
{
    if (k->command.open && k->command_backdrop && target == k->command_backdrop) {
        k->command.open = false;
    }
}

bool command_key(kit *k, Rml::Event &e, int key)
{
    if (!k->command.open) {
        return false;
    }
    unsigned mods = event_modifiers(e);
    CommandState &c = k->command;
    int count = (int)c.shown.size();
    bool ctrl = mods & KIT_MOD_CTRL;
    if (key == Rml::Input::KI_ESCAPE) {
        c.open = false;
    } else if (key == Rml::Input::KI_DOWN || (ctrl && (key == Rml::Input::KI_N || key == Rml::Input::KI_J))) {
        c.selected = std::min(c.selected + 1, count - 1);
        c.reveal = true;
    } else if (key == Rml::Input::KI_UP || (ctrl && (key == Rml::Input::KI_P || key == Rml::Input::KI_K))) {
        c.selected = std::max(c.selected - 1, 0);
        c.reveal = true;
    } else if (key == Rml::Input::KI_HOME || key == Rml::Input::KI_END) {
        c.selected = key == Rml::Input::KI_HOME ? 0 : std::max(count - 1, 0);
        c.reveal = true;
    } else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
        if (c.selected >= 0 && c.selected < count) {
            c.chosen = c.shown[(size_t)c.selected];
        }
    } else {
        return true;
    }
    e.StopPropagation();
    return true;
}

bool command_change(kit *k, Rml::Element *target)
{
    if (!k->command.open || target != k->command.input) {
        return false;
    }
    k->command.query = static_cast<Rml::ElementFormControlInput *>(target)->GetValue();
    k->command.selected = 0;
    k->command.reveal = true;
    return true;
}

void command_click(kit *k, Node *n)
{
    k->command.chosen = n->w.pending_index;
}

void command_hover(kit *k, Node *n)
{
    auto at = std::find(k->command.shown.begin(), k->command.shown.end(), n->w.pending_index);
    if (at != k->command.shown.end()) {
        k->command.selected = (int)(at - k->command.shown.begin());
    }
}

} // namespace kit_impl

using namespace kit_impl;

/* ---------- Command palette ---------- */

extern "C" void kit_command_open(kit *k, const char *placeholder)
{
    close_all_popups(k);
    k->command = CommandState{};
    k->command.open = true;
    k->command.focus = true;
    k->command.placeholder = placeholder ? placeholder : "";
}

extern "C" bool kit_command_is_open(const kit *k)
{
    return k->command.open;
}

extern "C" const char *kit_command_query(const kit *k)
{
    return k->command.query.c_str();
}

static int command(kit *k, const kit_command_item *items, int count, bool ranked);

extern "C" int kit_command(kit *k, const kit_command_item *items, int count)
{
    return command(k, items, count, false);
}

extern "C" int kit_command_ranked(kit *k, const kit_command_item *items, int count)
{
    return command(k, items, count, true);
}

static int command(kit *k, const kit_command_item *items, int count, bool ranked)
{
    CommandState &c = k->command;
    if (!c.open) {
        return -1;
    }
    Node *backdrop = emit_in(k, k->layer, "@command-backdrop", Kind::Box, "div", "command-backdrop");
    presence(k, backdrop);
    k->command_backdrop = backdrop->el;
    Node *n = emit_in(k, k->layer, "@command", Kind::Command, "div", "command");
    presence(k, n);
    if (!n->w.input_el) {
        Rml::Element *row = raw(k, n->el, "div", "cmd-input");
        raw_icon(k, row, "", KIT_ICON_SEARCH);
        Rml::ElementPtr ptr = k->doc->CreateElement("input");
        ptr->SetAttribute("type", "text");
        n->w.input_el = row->AppendChild(std::move(ptr));
        Rml::Element *list = raw(k, n->el, "div", "cmd-list");
        Rml::Element *footer = raw(k, n->el, "div", "cmd-footer");
        raw(k, footer, "span", "kbd-key", "\xe2\x86\x91");
        raw(k, footer, "span", "kbd-key", "\xe2\x86\x93");
        raw(k, footer, "span", "hint", "Navigate");
        raw(k, footer, "span", "kbd-key", "\xe2\x86\xb5");
        raw(k, footer, "span", "hint", "Open");
        raw(k, footer, "span", "spacer");
        raw(k, footer, "span", "kbd-key", "esc");
        raw(k, footer, "span", "hint", "Close");
        n->container = list;
    }
    c.input = n->w.input_el;
    c.input->SetAttribute("placeholder", c.placeholder);
    if (c.focus) {
        c.focus = false;
        static_cast<Rml::ElementFormControlInput *>(c.input)->SetValue("");
    }
    /* The dialog is modal: focus stays in its input (cmdk). A focus request made
       while the input was being created can be refused, which dropped the
       first typed characters. */
    if (k->context->GetFocusElement() != c.input) {
        c.input->Focus(true);
    }
    /* Rank with command-score; groups order by their best match (cmdk). */
    struct Scored
    {
        int index;
        double score;
    };
    std::vector<Scored> scored;
    for (int i = 0; i < count; ++i) {
        double s = ranked ? count - i : 1;
        if (!ranked && !c.query.empty()) {
            std::string text = std::string(items[i].label) + (items[i].keywords ? std::string(" ") + items[i].keywords : "");
            s = command_score(text, c.query);
            if (s <= (c.query.size() > 1 ? 0.02 : 0)) {
                continue;
            }
        }
        scored.push_back({ i, s });
    }
    std::vector<std::string> groups;
    std::map<std::string, std::vector<Scored>> by_group;
    for (const Scored &s : scored) {
        std::string g = items[s.index].group ? items[s.index].group : "";
        if (!by_group.count(g)) {
            groups.push_back(g);
        }
        by_group[g].push_back(s);
    }
    if (!ranked && !c.query.empty()) {
        for (auto &[g, list] : by_group) {
            (void)g;
            std::stable_sort(list.begin(), list.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
            if (list.size() > 8) {
                list.resize(8);
            }
        }
        std::stable_sort(groups.begin(), groups.end(), [&](const std::string &a, const std::string &b) {
            return by_group[a].front().score > by_group[b].front().score;
        });
    }
    c.shown.clear();
    for (const std::string &g : groups) {
        for (const Scored &s : by_group[g]) {
            c.shown.push_back(s.index);
        }
    }
    c.selected = std::clamp(c.selected, 0, std::max(0, (int)c.shown.size() - 1));
    push_node(k, n);
    int row = 0;
    for (const std::string &g : groups) {
        if (!g.empty()) {
            std::string key = "g:" + g;
            Node *h = emit(k, key.c_str(), Kind::Box, "div", "cmd-group");
            set_text(h, g.c_str());
        }
        for (const Scored &s : by_group[g]) {
            const kit_command_item &it = items[s.index];
            char key[32];
            std::snprintf(key, sizeof key, "i%d", s.index);
            Node *item = emit(k, key, Kind::CommandItem, "div", "cmd-item");
            Widget &w = item->w;
            if (!w.label_el) {
                w.icon_el = raw_icon(k, item->el, "", 0);
                w.label_el = raw(k, item->el, "span", "label");
                w.meta_el = raw(k, item->el, "span", "meta");
                w.kbd_el = raw(k, item->el, "span", "kbd");
            }
            w.pending_index = s.index;
            if (w.icon != it.icon) {
                w.icon = it.icon;
                w.icon_el->SetInnerRML(it.icon ? utf8(it.icon) : "");
            }
            /* Bold the matched characters when they appear in order. */
            std::string label = it.label ? it.label : "", html;
            size_t qi = 0;
            for (char ch : label) {
                if (qi < c.query.size() && std::tolower((unsigned char)ch) == std::tolower((unsigned char)c.query[qi])) {
                    html += "<b>" + escape(std::string(1, ch).c_str()) + "</b>";
                    ++qi;
                } else {
                    html += escape(std::string(1, ch).c_str());
                }
            }
            if (qi != c.query.size()) {
                html = escape(label.c_str());
            }
            if (w.label != html) {
                w.label = html;
                w.label_el->SetInnerRML(html);
            }
            set_element_text(w.meta_el, it.meta ? it.meta : "");
            set_element_text(w.kbd_el, it.shortcut ? it.shortcut : "");
            bool selected = row == c.selected;
            /* Keyboard moves reveal the selection with the least scrolling; hovering
               selects without scrolling (cmdk). */
            if (selected && c.reveal) {
                item->el->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
                c.reveal = false;
            }
            set_flag(item, "data-selected", selected);
            ++row;
        }
    }
    if (c.shown.empty()) {
        Node *empty = emit(k, "@empty", Kind::Box, "div", "cmd-empty");
        std::string text = "No results for \"" + c.query + "\"";
        set_text(empty, text.c_str());
    }
    pop_node(k);
    int chosen = c.chosen;
    if (chosen >= 0) {
        c.open = false;
        c.input->Blur();
    }
    c.chosen = -1;
    return chosen;
}
