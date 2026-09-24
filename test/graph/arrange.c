#include "graph/authoring/arrange.h"

#include "support/text_file.h"
#include "test/suite_internal.h"

#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

/* Mirrors graph_layout's fixed measurement so the benchmark times the
 * layout alone. */
typedef struct arrange_case
{
    arrange_box *boxes;
    arrange_link *links;
    arrange_point *seeds;
} arrange_case;

static int arrange_node_index(const graph *g, uint64_t id)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->id == id) {
            return i;
        }
    }
    return -1;
}

static arrange_case arrange_case_of(const graph *g)
{
    arrange_case c = { 0 };
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
        apush(
            c.boxes,
            ((arrange_box){ .width = 220,
                            .height = 36 + 24 * rows,
                            .title_height = 28,
                            .row_height = 24,
                            .pad = 4 })
        );
        apush(c.seeds, ((arrange_point){ n->x, n->y }));
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        int from = arrange_node_index(g, e->from_id);
        int to = arrange_node_index(g, e->to_id);
        int from_pin, to_pin;
        if (from < 0 || to < 0 ||
            !graph_pin_by_id(g->nodes[from], e->from_pin_id, NULL, &from_pin) ||
            !graph_pin_by_id(g->nodes[to], e->to_pin_id, NULL, &to_pin)) {
            continue;
        }
        apush(c.links, ((arrange_link){ .from = from, .to = to, .from_pin = from_pin, .to_pin = to_pin }));
    }
    return c;
}

static void arrange_case_free(arrange_case *c)
{
    afree(c->boxes);
    afree(c->links);
    afree(c->seeds);
}

static arrange_graph arrange_input(const arrange_case *c)
{
    return (arrange_graph){ c->boxes, acount(c->boxes), c->links, acount(c->links) };
}

static const arrange_box arrange_test_box = { 200, 84, 28, 24, 4 };

static arrange_score arrange_measure_links(
    const arrange_point *pos,
    int count,
    const arrange_link *links,
    int link_count
)
{
    arrange_box boxes[8];
    for (int i = 0; i < count; i++) {
        boxes[i] = arrange_test_box;
    }
    return arrange_measure(&(arrange_graph){ boxes, count, links, link_count }, pos);
}

static void arrange_measure_checks()
{
    const arrange_point grid[] = { { 0, 0 }, { 0, 300 }, { 600, 0 }, { 600, 300 } };
    const arrange_link swapped[] = { { .from = 0, .to = 3 }, { .from = 1, .to = 2 } };
    const arrange_link parallel[] = { { .from = 0, .to = 2 }, { .from = 1, .to = 3 } };
    const arrange_link fanned[] = { { .from = 0, .to = 2 }, { .from = 0, .to = 3 } };
    test_check(arrange_measure_links(grid, 4, swapped, 2).wire_crossings == 1, "arrange counts crossing wires");
    test_check(arrange_measure_links(grid, 4, parallel, 2).wire_crossings == 0, "arrange ignores parallel wires");
    test_check(arrange_measure_links(grid, 4, fanned, 2).wire_crossings == 0, "arrange ignores wires sharing a pin");

    /* A backward wire keeps its looping curve, straight through the middle. */
    const arrange_point row[] = { { 0, 0 }, { 400, -20 }, { 800, 0 } };
    const arrange_link back[] = { { .from = 2, .to = 0 } };
    arrange_score through = arrange_measure_links(row, 3, back, 1);
    test_check(through.wire_node_hits == 1 && through.node_overlaps == 0, "arrange counts a wire through an unrelated node");

    const arrange_point stacked[] = { { 0, 0 }, { 100, 40 } };
    test_check(arrange_measure_links(stacked, 2, NULL, 0).node_overlaps == 1, "arrange counts overlapping nodes");
}

static void arrange_route_checks()
{
    arrange_box boxes[3] = { arrange_test_box, arrange_test_box, arrange_test_box };
    const arrange_point row[] = { { 0, 0 }, { 400, -20 }, { 800, 0 } };
    const arrange_link links[] = { { .from = 0, .to = 2 },
                                   { .from = 0, .to = 1, .from_pin = 1 } };
    arrange_graph g = { boxes, 3, links, 2 };
    arrange_routes routes = { 0 };
    arrange_route(&g, row, &routes);
    bool clear = routes.start[1] > routes.start[0];
    for (int i = routes.start[0]; i < routes.start[1]; i++) {
        float y = routes.points[i].y;
        clear &= y < row[1].y || y > row[1].y + arrange_test_box.height;
    }
    test_check(clear, "arrange routes a wire past the node in a column it skips");
    test_check(routes.start[2] == routes.start[1], "arrange leaves wires between neighbouring columns unrouted");
    test_check(arrange_measure(&g, row).wire_node_hits == 0, "arrange measures routed wires as drawn");

    arrange_point *first = NULL;
    for (int i = 0; i < acount(routes.points); i++) {
        apush(first, routes.points[i]);
    }
    arrange_route(&g, row, &routes);
    test_check(
        acount(first) == acount(routes.points) &&
            !memcmp(first, routes.points, sizeof *first * (size_t)acount(first)),
        "arrange routes are deterministic"
    );
    afree(first);

    arrange_route(&(arrange_graph){ 0 }, NULL, &routes);
    test_check(acount(routes.start) == 1 && !acount(routes.points), "arrange routes an empty graph");
    arrange_routes_free(&routes);
}

static void arrange_layout_checks()
{
    /* Fan-out, a long edge, a cycle, a self-loop, and a second component. */
    const arrange_link links[] = {
        { .from = 0, .to = 1 },
        { .from = 0, .to = 2, .from_pin = 1 },
        { .from = 0, .to = 4, .from_pin = 2 },
        { .from = 1, .to = 3 },
        { .from = 2, .to = 3, .to_pin = 1 },
        { .from = 3, .to = 4, .to_pin = 1 },
        { .from = 4, .to = 1, .from_pin = 1, .to_pin = 1 },
        { .from = 2, .to = 2, .from_pin = 1, .to_pin = 2 },
        { .from = 5, .to = 6 },
    };
    arrange_box boxes[7];
    arrange_point first[7];
    arrange_point second[7];
    for (int i = 0; i < 7; i++) {
        boxes[i] = arrange_test_box;
        first[i] = (arrange_point){ 0, 0 };
        second[i] = first[i];
    }
    arrange_graph g = { boxes, 7, links, (int)(sizeof links / sizeof *links) };
    arrange_layout(&g, first);
    arrange_layout(&g, second);
    test_check(!memcmp(first, second, sizeof first), "arrange is deterministic");
    test_check(arrange_measure(&g, first).node_overlaps == 0, "arrange leaves no overlapping nodes");
    test_check(first[0].x < first[1].x && first[1].x < first[3].x, "arrange ranks sources before their targets");

    /* A chain with wires fanning out past several of its columns. */
    const arrange_link fans[] = {
        { .from = 0, .to = 1 },
        { .from = 1, .to = 2 },
        { .from = 2, .to = 3 },
        { .from = 3, .to = 4 },
        { .from = 0, .to = 2, .from_pin = 1, .to_pin = 1 },
        { .from = 0, .to = 3, .from_pin = 2, .to_pin = 1 },
        { .from = 0, .to = 4, .from_pin = 1, .to_pin = 1 },
        { .from = 1, .to = 4, .from_pin = 1, .to_pin = 2 },
    };
    arrange_graph fanned = { boxes, 5, fans, (int)(sizeof fans / sizeof *fans) };
    arrange_layout(&fanned, first);
    test_check(arrange_measure(&fanned, first).wire_node_hits == 0, "arrange leaves lanes for wires that skip a column");
}

void run_graph_arrange_checks()
{
    arrange_measure_checks();
    arrange_route_checks();
    arrange_layout_checks();
}

static double arrange_now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1.0e6;
}

/* FNV-1a over the placed coordinates, to spot any change in the output. */
static unsigned arrange_hash(const arrange_point *pos, int count)
{
    unsigned hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)pos;
    for (size_t i = 0; i < (size_t)count * sizeof *pos; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static int arrange_compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void arrange_collect(const char *dir, char ***paths)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        struct stat info;
        if (stat(path, &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            arrange_collect(path, paths);
            continue;
        }
        size_t len = strlen(path);
        if (len > 5 && !strcmp(path + len - 5, ".json")) {
            apush(*paths, strdup(path));
        }
    }
    closedir(d);
}

static char **arrange_content_paths()
{
    char **paths = NULL;
    arrange_collect("content", &paths);
    DIR *projects = opendir("projects");
    struct dirent *entry;
    while (projects && (entry = readdir(projects))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char dir[512];
        snprintf(dir, sizeof dir, "projects/%s/content", entry->d_name);
        arrange_collect(dir, &paths);
    }
    if (projects) {
        closedir(projects);
    }
    qsort(paths, (size_t)acount(paths), sizeof *paths, arrange_compare_paths);
    return paths;
}

static behavior_asset *arrange_load(const char *path)
{
    char *json = text_file_read(path);
    if (!json || !strstr(json, "\"graphs\"")) {
        free(json);
        return NULL;
    }
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    return asset;
}

/* Times arrange_layout and arrange_route on every graph asset under content/
 * and projects/<name>/content/, run from the repository root. Prints one row
 * per graph plus totals so layout speed and quality can be compared across
 * changes. `only` limits the run to graph names containing it. */
int bench_graph_arrange(int repeats, const char *only)
{
    registry_validate();
    char **paths = arrange_content_paths();
    arrange_score total = { 0, 0, 0, 0.0f };
    double total_ms = 0.0;
    double worst_ms = 0.0;
    int graphs = 0;
    int points = 0;
    printf(
        "%-64s %5s %5s %8s %8s %5s %5s %5s %6s %9s %8s %8s\n",
        "graph",
        "nodes",
        "links",
        "ms",
        "route",
        "cross",
        "hits",
        "over",
        "points",
        "length",
        "cost",
        "hash"
    );
    for (int p = 0; p < acount(paths); p++) {
        behavior_asset *asset = arrange_load(paths[p]);
        for (int gi = 0; asset && gi < acount(asset->graphs); gi++) {
            char name[512];
            snprintf(name, sizeof name, "%s#%d", paths[p], gi);
            if (only && !strstr(name, only)) {
                continue;
            }
            arrange_case c = arrange_case_of(asset->graphs[gi]);
            if (acount(c.boxes) < 2) {
                arrange_case_free(&c);
                continue;
            }
            arrange_graph g = arrange_input(&c);
            arrange_point *pos = NULL;
            arrange_routes routes = { 0 };
            asetlen(pos, g.box_count);
            double layout_ms = 1.0e30;
            double route_ms = 1.0e30;
            for (int r = 0; r < repeats; r++) {
                memcpy(pos, c.seeds, (size_t)g.box_count * sizeof *pos);
                double start = arrange_now_ms();
                arrange_layout(&g, pos);
                double placed = arrange_now_ms();
                arrange_route(&g, pos, &routes);
                double routed = arrange_now_ms();
                layout_ms = math_min(layout_ms, placed - start);
                route_ms = math_min(route_ms, routed - placed);
            }
            arrange_score s = arrange_measure(&g, pos);
            printf(
                "%-64s %5d %5d %8.3f %8.3f %5d %5d %5d %6d %9.0f %8.2f %08x\n",
                name,
                g.box_count,
                g.link_count,
                layout_ms,
                route_ms,
                s.wire_crossings,
                s.wire_node_hits,
                s.node_overlaps,
                acount(routes.points),
                s.wire_length,
                arrange_cost(s),
                arrange_hash(pos, g.box_count)
            );
            total.wire_crossings += s.wire_crossings;
            total.wire_node_hits += s.wire_node_hits;
            total.node_overlaps += s.node_overlaps;
            total.wire_length += s.wire_length;
            total_ms += layout_ms + route_ms;
            worst_ms = math_max(worst_ms, layout_ms + route_ms);
            points += acount(routes.points);
            graphs++;
            arrange_routes_free(&routes);
            afree(pos);
            arrange_case_free(&c);
        }
        if (asset) {
            behavior_asset_free(asset);
        }
        free(paths[p]);
    }
    afree(paths);
    printf(
        "total graphs=%d ms=%.3f worst_ms=%.3f crossings=%d hits=%d "
        "overlaps=%d points=%d length=%.0f cost=%.2f\n",
        graphs,
        total_ms,
        worst_ms,
        total.wire_crossings,
        total.wire_node_hits,
        total.node_overlaps,
        points,
        total.wire_length,
        arrange_cost(total)
    );
    return 0;
}
