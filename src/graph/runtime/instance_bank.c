#include "graph/runtime/instance_bank.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define INSTANCE_PAGE_MAX 256
#define INSTANCE_PAGE_BYTES (64 * 1024)

typedef struct behavior_instance_page
{
    behavior_instance_bank *bank;
    struct behavior_instance_page *next, *previous;
    int capacity, variables, live, used, free_count;
    int free_slots[INSTANCE_PAGE_MAX];
    behavior_instance instances[];
} behavior_instance_page;

struct behavior_instance_bank
{
    behavior_instance_page *available;
    int pages, next_capacity;
};

static void remove_available(behavior_instance_page *page)
{
    if (page->previous) {
        page->previous->next = page->next;
    } else {
        page->bank->available = page->next;
    }
    if (page->next) {
        page->next->previous = page->previous;
    }
    page->next = page->previous = NULL;
}

static void add_available(behavior_instance_page *page)
{
    behavior_instance_bank *bank = page->bank;
    page->next = bank->available;
    if (page->next) {
        page->next->previous = page;
    }
    bank->available = page;
}

static behavior_instance_page *new_page(behavior_instance_bank *bank, int variables)
{
    assert(
        (size_t)variables <=
        (SIZE_MAX - sizeof(behavior_instance_page) - sizeof(behavior_instance)) /
            sizeof(pin_value)
    );
    size_t row_bytes = sizeof(behavior_instance) + (size_t)variables * sizeof(pin_value);
    int fit = (int)((INSTANCE_PAGE_BYTES - sizeof(behavior_instance_page)) / row_bytes);
    if (!fit) {
        fit = 1;
    }
    int capacity = bank->next_capacity;
    if (bank->next_capacity < INSTANCE_PAGE_MAX) {
        bank->next_capacity *= 2;
    }
    if (capacity > fit) {
        capacity = fit;
    }
    size_t bytes = sizeof(behavior_instance_page) + (size_t)capacity * row_bytes;
    behavior_instance_page *page = xcalloc(1, bytes);
    page->bank = bank;
    page->capacity = capacity;
    page->variables = variables;
    bank->pages++;
    add_available(page);
    return page;
}

behavior_instance *behavior_instance_bank_alloc(behavior_instance_bank **owner, int variables)
{
    assert(owner && variables >= 0);
    if (!*owner) {
        *owner = xcalloc(1, sizeof **owner);
        (*owner)->next_capacity = 8;
    }
    behavior_instance_bank *bank = *owner;
    behavior_instance_page *page = bank->available;
    while (page && page->variables != variables) {
        page = page->next;
    }
    if (!page) {
        page = new_page(bank, variables);
    }
    int index = page->free_count ? page->free_slots[--page->free_count] : page->used++;
    assert(index < page->capacity);
    behavior_instance *instance = &page->instances[index];
    memset(instance, 0, sizeof *instance);
    instance->page = page;
    if (++page->live == page->capacity) {
        remove_available(page);
    }
    return instance;
}

pin_value *behavior_instance_bank_values(behavior_instance *instance, int count)
{
    behavior_instance_page *page = instance->page;
    if (!page || !count || count != page->variables) {
        return NULL;
    }
    static_assert(alignof(behavior_instance) >= alignof(pin_value));
    pin_value *values = (pin_value *)(page->instances + page->capacity);
    return values + (size_t)(instance - page->instances) * (size_t)count;
}

void behavior_instance_bank_release(behavior_instance *instance)
{
    behavior_instance_page *page = instance->page;
    if (!page) {
        free(instance);
        return;
    }
    assert(page->live > 0 && page->free_count < page->capacity);
    page->free_slots[page->free_count++] = (int)(instance - page->instances);
    if (page->live-- == page->capacity) {
        add_available(page);
    }
    if (!page->live) {
        remove_available(page);
        page->bank->pages--;
        free(page);
    }
}

void behavior_instance_bank_free(behavior_instance_bank *bank)
{
    if (!bank) {
        return;
    }
    assert(!bank->pages && !bank->available);
    free(bank);
}
