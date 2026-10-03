/* Asset thumbnails (studio_thumbnails.h): a fixed table of slots, each a
 * canvas bound as the still image "kit:thumb:<slot>". An emission asks for
 * the thumbnails it shows; after it, queued ones render within a frame
 * budget, cheapest kinds first, large previews before tiles. A thumbnail
 * keeps its slot while emissions ask for it; a new one takes a free slot or
 * the one asked for least recently. Each knows the stamp of the files it
 * shows (the asset's and those it uses directly or through one asset), and
 * renders again, keeping its old picture meanwhile, when the asset index
 * rebuilds or a second passes with a different stamp. */
#include "editor/studio/studio_thumbnails.h"

#include "editor/studio/studio_internal.h"
#include "render/shader_catalog.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TILE_SLOTS 256
#define LARGE_SLOTS 4
#define SLOT_COUNT (TILE_SLOTS + LARGE_SLOTS)
/* Rendering stops for the frame once it has taken this long; one thumbnail
   always renders, however long it takes. */
#define BUDGET_SECONDS 0.004
#define CHECK_SECONDS 1.0

static const float SIZE_DP[STUDIO_THUMB_SIZE_COUNT] = { 128, 256 };
static const int SIZE_MAX_PIXELS[STUDIO_THUMB_SIZE_COUNT] = { 256, 512 };

typedef enum
{
    THUMB_FREE,
    THUMB_QUEUED,
    THUMB_READY,
    THUMB_FAILED,
} thumb_state;

typedef struct
{
    thumb_state state;
    reference_asset_kind kind;
    studio_thumb_size size;
    char *name, *path;
    uint64_t stamp; /* of the files its next or last picture shows */
    uint64_t asked; /* the emission that last asked for it */
    uint64_t order; /* when, among that emission's requests */
    int side;       /* the canvas's pixels */
    CF_Canvas canvas;
    bool shown; /* the canvas holds a picture, maybe of an older stamp */
    double ms;  /* spent on the picture in progress */
} thumb;

struct studio_thumbs
{
    thumb slots[SLOT_COUNT];
    char sources[SLOT_COUNT][24];
    char names[SLOT_COUNT][24];
    uint64_t requests;
    /* Direct dependencies by asset position, as of an index revision. */
    const reference_index *index;
    uint64_t index_revision;
    int *dependency_start, *dependencies;
    uint64_t validated; /* the index revision stamps were last checked at */
    double checked;     /* and when */
    int busy; /* the slot + 1 of a job that continues next frame, or 0 */
    studio_thumb_renderers *renderers;
    studio_thumb_stats stats;
};

static studio_thumbs *thumbs_new()
{
    studio_thumbs *t = xcalloc(1, sizeof *t);
    for (int i = 0; i < SLOT_COUNT; i++) {
        snprintf(t->names[i], sizeof t->names[i], "thumb:%d", i);
        snprintf(t->sources[i], sizeof t->sources[i], "kit:thumb:%d", i);
    }
    return t;
}

/* ---------- Stamps ---------- */

static uint64_t mix(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ (value >> (8 * i) & 0xff)) * 1099511628211ull;
    }
    return hash;
}

/* What changes when the file does: its modification time and size. */
static uint64_t file_stamp(const char *path)
{
    struct stat info;
    if (!path || stat(path, &info) != 0) {
        return 0;
    }
#ifdef __APPLE__
    uint64_t nanoseconds = (uint64_t)info.st_mtimespec.tv_nsec;
#else
    uint64_t nanoseconds = (uint64_t)info.st_mtim.tv_nsec;
#endif
    return mix(
        mix((uint64_t)info.st_mtime * 1000000000ull + nanoseconds, 0),
        (uint64_t)info.st_size
    );
}

static void dependencies_free(studio_thumbs *t)
{
    afree(t->dependency_start);
    afree(t->dependencies);
    t->dependency_start = t->dependencies = NULL;
}

/* Each asset's direct dependencies, from the references it holds. */
static void index_dependencies(studio_thumbs *t, const reference_index *index, uint64_t revision)
{
    if (t->index == index && t->index_revision == revision && t->dependency_start) {
        return;
    }
    dependencies_free(t);
    t->index = index;
    t->index_revision = revision;
    int assets = reference_index_asset_count(index);
    asetlen(t->dependency_start, assets + 1);
    memset(t->dependency_start, 0, (size_t)(assets + 1) * sizeof *t->dependency_start);
    for (int i = 0; i < reference_index_count(index); i++) {
        const reference *r = reference_index_at(index, i);
        if (reference_dependency(r) >= 0 && r->source >= 0) {
            t->dependency_start[r->source + 1]++;
        }
    }
    for (int i = 0; i < assets; i++) {
        t->dependency_start[i + 1] += t->dependency_start[i];
    }
    asetlen(t->dependencies, t->dependency_start[assets]);
    int *fill = xcalloc((size_t)assets + 1, sizeof *fill);
    for (int i = 0; i < reference_index_count(index); i++) {
        const reference *r = reference_index_at(index, i);
        int used = reference_dependency(r);
        if (used >= 0 && r->source >= 0) {
            t->dependencies[t->dependency_start[r->source] + fill[r->source]++] = used;
        }
    }
    free(fill);
}

static uint64_t asset_stamp(const reference_index *index, int asset)
{
    return file_stamp(reference_index_asset_at(index, asset)->path);
}

/* The asset's file and those it uses, directly or through one asset. */
static uint64_t stamp_of(studio *s, studio_thumbs *t, int asset)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    index_dependencies(t, index, s->scene->asset_index_revision);
    const reference_asset *a = reference_index_asset_at(index, asset);
    uint64_t stamp = mix(1469598103934665603ull, file_stamp(a->path));
    for (int i = t->dependency_start[asset]; i < t->dependency_start[asset + 1]; i++) {
        int used = t->dependencies[i];
        stamp = mix(stamp, asset_stamp(index, used));
        for (int j = t->dependency_start[used]; j < t->dependency_start[used + 1]; j++) {
            stamp = mix(stamp, asset_stamp(index, t->dependencies[j]));
        }
    }
    if (a->kind == REFERENCE_ASSET_MATERIAL || a->kind == REFERENCE_ASSET_SHAPE) {
        stamp = mix(stamp, render_shader_revision());
    }
    if (a->kind == REFERENCE_ASSET_UI) {
        stamp = mix(stamp, s->light);
    }
    return stamp;
}

/* ---------- Slots ---------- */

static void release(studio *s, studio_thumbs *t, int slot)
{
    thumb *e = &t->slots[slot];
    if (t->busy == slot + 1) {
        studio_thumb_cancel(t->renderers);
        t->busy = 0;
    }
    if (e->canvas.id) {
        rml_backend_set_image(s->backend, t->names[slot], 0);
        cf_destroy_canvas(e->canvas);
        t->stats.bound--;
    }
    free(e->name);
    free(e->path);
    *e = (thumb){ 0 };
}

static int find(const studio_thumbs *t, const reference_asset *a, studio_thumb_size size)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        const thumb *e = &t->slots[i];
        if (e->state != THUMB_FREE && e->kind == a->kind && e->size == size &&
            !strcmp(e->name, a->name)) {
            return i;
        }
    }
    return -1;
}

/* A free slot of the size, or the one asked for least recently that this
   emission has not asked for; -1 when this emission asks for them all. */
static int claim(studio *s, studio_thumbs *t, studio_thumb_size size)
{
    int first = size == STUDIO_THUMB_LARGE ? TILE_SLOTS : 0;
    int end = size == STUDIO_THUMB_LARGE ? SLOT_COUNT : TILE_SLOTS;
    int oldest = -1;
    for (int i = first; i < end; i++) {
        const thumb *e = &t->slots[i];
        if (e->state == THUMB_FREE) {
            return i;
        }
        if (e->asked != s->ui_schedule.frames &&
            (oldest < 0 || e->asked < t->slots[oldest].asked)) {
            oldest = i;
        }
    }
    if (oldest >= 0) {
        release(s, t, oldest);
        t->stats.evicted++;
    }
    return oldest;
}

static int side_of(studio *s, studio_thumb_size size)
{
    int side = (int)(SIZE_DP[size] * studio_pixel_ratio(s) + 0.5f);
    return side < SIZE_MAX_PIXELS[size] ? side : SIZE_MAX_PIXELS[size];
}

const char *studio_thumb(studio *s, int asset, studio_thumb_size size)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    const reference_asset *a = reference_index_asset_at(index, asset);
    if (!a || !a->name || !a->path || a->unresolved) {
        return NULL;
    }
    if (!s->thumbs) {
        s->thumbs = thumbs_new();
    }
    studio_thumbs *t = s->thumbs;
    int slot = find(t, a, size);
    if (slot < 0 && (slot = claim(s, t, size)) >= 0) {
        thumb *e = &t->slots[slot];
        *e = (thumb){ .state = THUMB_QUEUED,
                      .kind = a->kind,
                      .size = size,
                      .name = xstrdup(a->name),
                      .path = xstrdup(a->path),
                      .stamp = stamp_of(s, t, asset) };
    }
    if (slot < 0) {
        return NULL;
    }
    thumb *e = &t->slots[slot];
    if (strcmp(e->path, a->path)) {
        free(e->path);
        e->path = xstrdup(a->path);
        e->stamp = stamp_of(s, t, asset);
        e->state = THUMB_QUEUED;
    }
    e->asked = s->ui_schedule.frames;
    e->order = t->requests++;
    return e->shown ? t->sources[slot] : NULL;
}

const char *studio_thumb_cached(studio *s, int asset)
{
    const reference_asset
        *a = reference_index_asset_at(app_scene_authoring_asset_index(s->scene), asset);
    int slot = s->thumbs && a && a->name ? find(s->thumbs, a, STUDIO_THUMB_TILE) : -1;
    if (slot < 0 || !s->thumbs->slots[slot].shown) {
        return NULL;
    }
    s->thumbs->slots[slot].asked = s->ui_schedule.frames;
    return s->thumbs->sources[slot];
}

/* ---------- Rendering ---------- */

/* Thumbnails the latest emission asked for whose files changed. */
static void validate(studio *s, studio_thumbs *t, uint64_t latest)
{
    if (t->validated == s->scene->asset_index_revision &&
        s->time - t->checked < CHECK_SECONDS) {
        return;
    }
    t->validated = s->scene->asset_index_revision;
    t->checked = s->time;
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    for (int i = 0; i < SLOT_COUNT; i++) {
        thumb *e = &t->slots[i];
        if (e->state == THUMB_FREE || e->asked != latest) {
            continue;
        }
        int asset = reference_index_find_name(index, e->kind, e->name);
        uint64_t stamp = asset >= 0 ? stamp_of(s, t, asset) : e->stamp;
        if (stamp != e->stamp) {
            e->stamp = stamp;
            e->state = THUMB_QUEUED;
        }
    }
}

/* Large previews first, then the cheapest kinds, then in the order asked. */
static int next_job(const studio_thumbs *t, uint64_t latest)
{
    int best = -1;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const thumb *e = &t->slots[i];
        if (e->state != THUMB_QUEUED || e->asked != latest) {
            continue;
        }
        if (best < 0) {
            best = i;
            continue;
        }
        const thumb *b = &t->slots[best];
        int cost = studio_thumb_cost(e->kind), best_cost = studio_thumb_cost(b->kind);
        if (e->size != b->size  ? e->size > b->size
            : cost != best_cost ? cost < best_cost
                                : e->order < b->order) {
            best = i;
        }
    }
    return best;
}

static double seconds()
{
    return (double)cf_get_ticks() / (double)cf_get_tick_frequency();
}

/* One step of the slot's job; true while it continues next frame. */
static bool render(studio *s, studio_thumbs *t, int slot)
{
    thumb *e = &t->slots[slot];
    int side = studio_thumb_side(e->kind, side_of(s, e->size));
    if (e->canvas.id && e->side != side) {
        rml_backend_set_image(s->backend, t->names[slot], 0);
        cf_destroy_canvas(e->canvas);
        e->canvas = (CF_Canvas){ 0 };
        e->shown = false;
        t->stats.bound--;
    }
    if (!e->canvas.id) {
        e->canvas = cf_make_canvas(cf_canvas_defaults(side, side));
        e->side = side;
        t->stats.bound++;
    }
    studio_thumb_job job = { e->kind, e->size, e->name, e->path, side, s->light };
    double start = seconds();
    studio_thumb_step step = studio_thumb_render(s, &t->renderers, &job, e->canvas);
    double ms = (seconds() - start) * 1000;
    e->ms += ms;
    if (ms > t->stats.max_ms[e->kind]) {
        t->stats.max_ms[e->kind] = ms;
    }
    if (step == STUDIO_THUMB_BUSY) {
        return true;
    }
    bool drawn = step == STUDIO_THUMB_DRAWN;
    t->stats.count[e->kind]++;
    t->stats.total_ms[e->kind] += e->ms;
    t->stats.blank += !drawn;
    e->ms = 0;
    e->state = drawn ? THUMB_READY : THUMB_FAILED;
    e->shown |= drawn;
    if (drawn) {
        rml_backend_set_image(
            s->backend,
            t->names[slot],
            cf_canvas_get_target(e->canvas).id
        );
    }
    return false;
}

void studio_thumbs_frame(studio *s)
{
    studio_thumbs *t = s->thumbs;
    if (!t || !s->ui_schedule.frames) {
        return;
    }
    uint64_t latest = s->ui_schedule.frames - 1;
    validate(s, t, latest);
    double start = seconds();
    for (;;) {
        int slot = t->busy ? t->busy - 1 : next_job(t, latest);
        if (slot < 0) {
            return;
        }
        bool busy = render(s, t, slot);
        t->busy = busy ? slot + 1 : 0;
        s->ui_schedule.wake |= !busy;
        if (busy || seconds() - start >= BUDGET_SECONDS) {
            return;
        }
    }
}

bool studio_thumbs_pending(const studio *s)
{
    const studio_thumbs *t = s->thumbs;
    return t && s->ui_schedule.frames &&
           (t->busy || next_job(t, s->ui_schedule.frames - 1) >= 0);
}

studio_thumb_stats studio_thumbs_stats(const studio *s)
{
    return s->thumbs ? s->thumbs->stats : (studio_thumb_stats){ 0 };
}

void studio_thumbs_free(studio *s)
{
    studio_thumbs *t = s->thumbs;
    if (!t) {
        return;
    }
    for (int i = 0; i < SLOT_COUNT; i++) {
        release(s, t, i);
    }
    studio_thumb_renderers_free(t->renderers);
    dependencies_free(t);
    free(t);
    s->thumbs = NULL;
}
