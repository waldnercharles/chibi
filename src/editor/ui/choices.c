#include "editor/ui/choices.h"

#include "graph/model/registry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int editor_choices_count(const editor_choices *choices)
{
    return choices->enumeration ? choices->enumeration->value_count : choices->count;
}

editor_choice editor_choice_at(const editor_choices *choices, int index)
{
    if (!choices->enumeration) {
        return choices->items[index];
    }
    const enum_value_desc *value = &choices->enumeration->values[index];
    return (editor_choice){ .value = value->value,
                            .label = value->label,
                            .name = value->name };
}

void editor_choices_format(int value, const editor_choices *choices, char *buf, size_t cap)
{
    if (!cap) {
        return;
    }
    for (int i = 0; i < editor_choices_count(choices); i++) {
        editor_choice choice = editor_choice_at(choices, i);
        assert(choice.label && choice.label[0]);
        if (choice.value == value) {
            snprintf(buf, cap, "%s", choice.label);
            return;
        }
    }
    snprintf(buf, cap, "Unknown (%d)", value);
}

const editor_choices *editor_choices_for_enum(const char *type_name)
{
    static editor_choices choices;
    const enum_desc *enumeration = registry_enum_by_name(type_name);
    if (!enumeration) {
        return NULL;
    }
    choices = (editor_choices){ .enumeration = enumeration };
    return &choices;
}
