#include "graph/compiler/behavior_program.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/text_file.h"

#include <assert.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>

static behavior_asset **catalog(const char *pattern)
{
    glob_t paths = { 0 };
    assert(glob(pattern, 0, NULL, &paths) == 0);
    behavior_asset **assets = NULL;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        char *text = text_file_read(paths.gl_pathv[i]);
        assert(text);
        behavior_asset *a = behavior_asset_load_json(text);
        if (!a) {
            fprintf(stderr, "%s: %s\n", paths.gl_pathv[i], graph_last_error());
        }
        assert(a);
        apush(assets, a);
        free(text);
    }
    globfree(&paths);
    return assets;
}

static bool audit_entry(const behavior_asset *a, graph *g, node *entry, const char *kind)
{
    const behavior_graph_program *body = behavior_program_graph(a->program, g);
    bool whole = behavior_program_function(body, entry) != NULL;
    const behavior_function_diagnostic *d = behavior_program_function_diagnostic(body, entry);
    assert(whole || d);
    printf(
        "%s_%s %s.%s",
        kind,
        whole ? "WHOLE" : "EXCLUDED",
        a->name,
        entry->desc == &NODE_Event ? entry->reference_name : g->name
    );
    if (d) {
        printf(
            " graph=%llu node=%llu type=%s pin=%d output=%d reason=%s",
            (unsigned long long)d->graph->graph_id,
            (unsigned long long)d->node->id,
            d->node->desc->name,
            d->pin,
            d->output,
            d->reason
        );
    }
    putchar('\n');
    return whole;
}

/* Explicit acceptance gate for events and every local/library function body.
 * Shader libraries and event declarations are supporting systems. */
int whole_function_audit()
{
    behavior_asset **assets = catalog("content/behaviors/*.yaml");
    behavior_asset **libraries = catalog("content/libraries/*.yaml");
    behavior_asset **events = catalog("content/events/*.yaml");
    for (int i = 0; i < acount(events); i++) {
        apush(libraries, events[i]);
    }
    afree(events);
    int total[4] = { 0 }, compiled[4] = { 0 };
    int asset_count = acount(assets);
    for (int i = 0; i < acount(libraries); i++) {
        if (libraries[i]->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(assets, libraries[i]);
        }
    }
    for (int i = 0; i < acount(assets); i++) {
        behavior_asset *a = assets[i];
        char message[256];
        behavior_asset *linked = behavior_asset_link_libraries(
            a,
            (const behavior_asset *const *)libraries,
            acount(libraries),
            message,
            sizeof message
        );
        if (!linked) {
            fprintf(stderr, "%s: %s\n", a->name, message);
        }
        assert(linked);
        behavior_program *program = behavior_program_publish(linked);
        const compile_diagnostic *error = behavior_program_diagnostic(program);
        if (error) {
            fprintf(stderr, "%s: %s\n", a->name, error->message);
        }
        assert(!error);
        for (int j = 0; j < acount(a->graphs); j++) {
            graph *source = behavior_asset_find_graph(linked, a->graphs[j]->graph_id);
            if (source->kind != GRAPH_FUNCTION) {
                continue;
            }
            node *entry = graph_node_by_id(source, source->entry_id);
            bool pure = !entry->output_count || entry->output_pins[0].kind != PIN_EXEC;
            int group = i >= asset_count ? 3 : pure ? 2 : 1;
            total[group]++;
            compiled[group] += audit_entry(
                linked,
                source,
                entry,
                group == 3 ? "LIBRARY"
                : pure     ? "PURE"
                           : "FUNCTION"
            );
        }
        if (i < asset_count) {
            for (int j = 0; j < acount(linked->graph->nodes); j++) {
                node *n = linked->graph->nodes[j];
                if (n->desc == &NODE_Event) {
                    total[0]++;
                    compiled[0] += audit_entry(linked, linked->graph, n, "EVENT");
                }
            }
        }
        behavior_asset_free(linked);
    }
    printf(
        "whole_function_entries=%d/%d assets=%d local_functions=%d "
        "library_functions=%d\n",
        compiled[0],
        total[0],
        asset_count,
        total[1] + total[2],
        total[3]
    );
    printf(
        "whole_effectful_function_bodies=%d/%d "
        "whole_pure_function_bodies=%d/%d "
        "whole_library_function_bodies=%d/%d\n",
        compiled[1],
        total[1],
        compiled[2],
        total[2],
        compiled[3],
        total[3]
    );
    for (int i = 0; i < asset_count; i++) {
        behavior_asset_free(assets[i]);
    }
    for (int i = 0; i < acount(libraries); i++) {
        behavior_asset_free(libraries[i]);
    }
    afree(libraries);
    afree(assets);
    return compiled[0] == total[0] && compiled[1] == total[1] &&
                   compiled[2] == total[2] && compiled[3] == total[3]
             ? 0
             : 1;
}
