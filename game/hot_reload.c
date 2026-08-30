#include "hot_reload.h"

#include "../core/array.h"
#include "../core/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_hot_module
{
    game_hot_image active;
};

static void report_message(game_hot_report *report, const char *format, ...)
{
    if (!report) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(report->message, sizeof report->message, format, args);
    va_end(args);
    report->failed = 1;
}

static void report_reset(game_hot_report *report)
{
    if (report) {
        *report = (game_hot_report){ 0 };
    }
}

game_hot_module *game_hot_module_new(const game_hot_image *image)
{
    if (!image || !image->module_name || !image->resolve_node ||
        image->component_count < 0 || (image->component_count && !image->components)) {
        return NULL;
    }
    game_hot_module *module = (game_hot_module *)xcalloc(1, sizeof *module);
    module->active = *image;
    return module;
}

void game_hot_module_free(game_hot_module *module)
{
    free(module);
}

const game_hot_image *game_hot_module_active(const game_hot_module *module)
{
    return module ? &module->active : NULL;
}

static int validate_behavior_asset(
    const behavior_asset *definition,
    const game_hot_image *image,
    game_hot_report *report
)
{
    for (int g = 0; g < acount(definition->graphs); g++) {
        const graph *graph = definition->graphs[g];
        for (int i = 0; i < acount(graph->nodes); i++) {
            const node *node = graph->nodes[i];
            const node_desc
                *replacement = image->resolve_node(node->desc->name, image->resolve_user);
            if (!replacement || replacement->input_count != node->input_count ||
                replacement->output_count != node->output_count) {
                report_message(
                    report,
                    "descriptor %s is unavailable or changed shape",
                    node->desc->name
                );
                return -1;
            }
        }
    }
    return 0;
}

int game_hot_rebind_behavior_asset(
    behavior_asset *definition,
    const game_hot_image *image,
    game_hot_report *report
)
{
    if (!definition || !image || !image->resolve_node) {
        report_message(report, "invalid hot-reload image");
        return -1;
    }
    if (validate_behavior_asset(definition, image, report)) {
        return -1;
    }
    for (int g = 0; g < acount(definition->graphs); g++) {
        graph *graph = definition->graphs[g];
        for (int i = 0; i < acount(graph->nodes); i++) {
            node *node = graph->nodes[i];
            node->desc = image->resolve_node(node->desc->name, image->resolve_user);
            if (report) {
                report->rebound_nodes++;
            }
        }
    }
    return 0;
}

int game_hot_reload_components(ecs_world *world, const game_hot_image *image, game_hot_report *report)
{
    report_reset(report);
    if (!world || !image) {
        report_message(report, "invalid component reload image");
        return -1;
    }
    for (int i = 0; i < image->component_count; i++) {
        ecs_migration_report migration = { 0 };
        int result = ecs_world_migrate_component(world, image->components[i], &migration);
        if (result < 0) {
            report_message(
                report,
                "component %s could not migrate",
                image->components[i]->name
            );
            return -1;
        }
        if (report) {
            report->changed_components += migration.changed;
            report->preserved_fields += migration.preserved_fields;
            report->zeroed_fields += migration.zeroed_fields;
        }
    }
    return 0;
}

int game_hot_module_reload(
    game_hot_module *module,
    const game_hot_image *candidate,
    ecs_world *world,
    behavior_asset *const *definitions,
    int definition_count,
    game_hot_report *report
)
{
    report_reset(report);
    if (!GAME_HOT_RELOAD || !module || !candidate || !world ||
        candidate->module_name == NULL || candidate->component_count < 0 ||
        (candidate->component_count && !candidate->components) ||
        definition_count < 0 || (definition_count && !definitions)) {
        report_message(report, "hot reload is unavailable or arguments are invalid");
        return -1;
    }
    for (int i = 0; i < definition_count; i++) {
        if (validate_behavior_asset(definitions[i], candidate, report)) {
            return -1;
        }
    }
    if (game_hot_reload_components(world, candidate, report)) {
        return -1;
    }
    for (int i = 0; i < definition_count; i++) {
        if (game_hot_rebind_behavior_asset(definitions[i], candidate, report)) {
            return -1;
        }
    }
    module->active = *candidate;
    if (report) {
        snprintf(
            report->message,
            sizeof report->message,
            "reloaded %s",
            candidate->module_name
        );
    }
    return 0;
}

int game_hot_reload_available(void)
{
    return GAME_HOT_RELOAD != 0;
}
