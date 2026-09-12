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

/* This explicit acceptance command fails until every catalog event qualifies.
 * Ordinary backend/parity tests remain independent of this incomplete gate. */
int whole_function_audit()
{
    behavior_asset **assets = catalog("content/behaviors/*.json");
    behavior_asset **libraries = catalog("content/libraries/*.json");
    behavior_asset **events = catalog("content/events/*.json");
    for (int i = 0; i < acount(events); i++) {
        apush(libraries, events[i]);
    }
    afree(events);
    int entries = 0, compiled = 0, functions = 0, library_functions = 0;
    for (int i = 0; i < acount(libraries); i++) {
        for (int j = 0; j < acount(libraries[i]->graphs); j++) {
            library_functions += libraries[i]->graphs[j]->kind == GRAPH_FUNCTION;
        }
    }
    for (int i = 0; i < acount(assets); i++) {
        behavior_asset *a = assets[i];
        for (int j = 0; j < acount(a->graphs); j++) {
            functions += a->graphs[j]->kind == GRAPH_FUNCTION;
        }
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
        const behavior_graph_program *g = behavior_program_graph(program, linked->graph);
        for (int j = 0; j < acount(linked->graph->nodes); j++) {
            node *n = linked->graph->nodes[j];
            if (n->desc != &NODE_Event) {
                continue;
            }
            entries++;
            const behavior_function_diagnostic *d = behavior_program_function_diagnostic(g, n);
            if (!d) {
                assert(behavior_program_function(g, n));
                compiled++;
                printf("WHOLE %s.%s\n", a->name, n->reference_name);
                continue;
            }
            printf(
                "EXCLUDED %s.%s graph=%llu node=%llu type=%s pin=%d output=%d "
                "reason=%s\n",
                a->name,
                n->reference_name,
                (unsigned long long)d->graph->graph_id,
                (unsigned long long)d->node->id,
                d->node->desc->name,
                d->pin,
                d->output,
                d->reason
            );
        }
        behavior_asset_free(linked);
        behavior_asset_free(a);
    }
    printf(
        "whole_function_entries=%d/%d assets=%d local_functions=%d "
        "library_functions=%d\n",
        compiled,
        entries,
        acount(assets),
        functions,
        library_functions
    );
    for (int i = 0; i < acount(libraries); i++) {
        behavior_asset_free(libraries[i]);
    }
    afree(libraries);
    afree(assets);
    return compiled == entries ? 0 : 1;
}
