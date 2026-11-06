#include "runtime/material_destruction.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct material_destruction
{
    material_damage *damage;
    material_damage *retry, *retry_work;
    uint64_t *release;
    material_event *events;
    int event_cursor;
    material_mask_work *work;
    unsigned char *before;
    bool stepping;
};

material_destruction *material_destruction_new()
{
    material_destruction *d = xcalloc(1, sizeof *d);
    d->work = material_mask_work_new();
    return d;
}

void material_destruction_reset(material_destruction *d)
{
    assert(!d->stepping);
    aclear(d->damage);
    aclear(d->retry);
    aclear(d->retry_work);
    aclear(d->release);
    aclear(d->events);
    d->event_cursor = 0;
}

void material_destruction_free(material_destruction *d)
{
    assert(!d->stepping);
    afree(d->damage);
    afree(d->retry);
    afree(d->retry_work);
    afree(d->release);
    afree(d->events);
    material_mask_work_free(d->work);
    afree(d->before);
    free(d);
}

void material_destruction_queue_damage(material_destruction *d, material_damage damage)
{
    assert(isfinite(damage.point.x) && isfinite(damage.point.y));
    assert(isfinite(damage.radius) && damage.radius >= 0);
    assert(isfinite(damage.energy) && damage.energy >= 0);
    apush(d->damage, damage);
}

void material_destruction_queue_release(material_destruction *d, uint64_t target)
{
    for (int i = 0; i < acount(d->release); i++) {
        if (d->release[i] == target) {
            return;
        }
    }
    apush(d->release, target);
}

static void queue_retry(material_destruction *d, material_damage damage)
{
    for (int i = 0; i < acount(d->retry); i++) {
        if (d->retry[i].target == damage.target) {
            return;
        }
    }
    apush(d->retry, damage);
}

void material_destruction_emit_event(material_destruction *d, material_event event)
{
    apush(d->events, event);
}

bool material_destruction_poll_event(material_destruction *d, material_event *event)
{
    if (d->event_cursor == acount(d->events)) {
        aclear(d->events);
        d->event_cursor = 0;
        return false;
    }
    *event = d->events[d->event_cursor++];
    return true;
}

static void publish(material_destruction *d, const material_host *host, material_event event)
{
    material_destruction_emit_event(d, event);
    host->result(host->user, &event);
}

typedef struct
{
    int emitted, restored;
} fragment_result;

static int compare_piece_size(const void *left, const void *right)
{
    const material_mask *a = left, *b = right;
    return (b->count > a->count) - (b->count < a->count);
}

static void restore_piece(material_mask *destination, const material_mask *piece)
{
    assert(destination->width == piece->width && destination->height == piece->height);
    assert(destination->cell_size == piece->cell_size);
    int length = destination->width * destination->height;
    if (piece->damage && !destination->damage) {
        destination->damage = xcalloc((size_t)length, sizeof *destination->damage);
    }
    for (int i = 0; i < length; i++) {
        if (!piece->cells[i] || destination->cells[i]) {
            continue;
        }
        destination->cells[i] = 1;
        destination->count++;
        destination->distance[i] = piece->distance[i];
        if (piece->damage) {
            destination->damage[i] = piece->damage[i];
        }
    }
}

static fragment_result fragments(
    material_destruction *d,
    const material_host *host,
    material_mask *pieces,
    material_mask *restore_to,
    bool restore_overflow,
    material_damage damage,
    float speed,
    int minimum,
    int *capacity
)
{
    fragment_result result = { 0 };
    if (acount(pieces) > 1) {
        asort(pieces, compare_piece_size);
    }
    for (int i = 0; i < acount(pieces); i++) {
        if (pieces[i].count < minimum) {
            material_mask_free(&pieces[i]);
            continue;
        }
        if (*capacity <= 0) {
            if (restore_overflow) {
                restore_piece(restore_to, &pieces[i]);
                result.restored += pieces[i].count;
                queue_retry(d, damage);
            }
            material_mask_free(&pieces[i]);
            continue;
        }
        result.emitted += pieces[i].count;
        (*capacity)--;
        host->create_fragment(
            host->user,
            damage.target,
            (material_fragment){ pieces[i], damage.point, speed }
        );
        publish(
            d,
            host,
            (material_event){ .kind = MATERIAL_EVENT_FRAGMENT,
                              .target = damage.target,
                              .point = damage.point,
                              .cause = damage.cause,
                              .impact = damage.impact }
        );
    }
    afree(pieces);
    return result;
}

static void retry_fragments(
    material_destruction *d,
    const material_host *host,
    material_damage damage,
    int *capacity
)
{
    material_source source;
    if (!host->read_source(host->user, damage.target, &source) ||
        source.mask->count < source.minimum) {
        return;
    }
    if (*capacity <= 0) {
        queue_retry(d, damage);
        return;
    }
    int discarded, previous = source.mask->count;
    material_mask *pieces = material_mask_detach_fragments(
        source.mask,
        source.minimum,
        source.anchored,
        &discarded,
        d->work
    );
    bool changed = previous != source.mask->count;
    if (changed) {
        material_mask_rebuild_work(source.mask, d->work);
    }
    fragment_result result = fragments(
        d,
        host,
        pieces,
        source.mask,
        true,
        damage,
        damage.cause == MATERIAL_DAMAGE_BLAST ? source.blast_speed : source.fragment_speed,
        source.minimum,
        capacity
    );
    if (result.restored) {
        material_mask_rebuild_work(source.mask, d->work);
    }
    if (changed || result.emitted || result.restored) {
        host->invalidate_geometry(host->user, damage.target);
    }
}

static void apply_damage(material_destruction *d, const material_host *host, int index, int *capacity)
{
    material_damage damage = d->damage[index];
    material_source source;
    if (!damage.energy || !host->read_source(host->user, damage.target, &source)) {
        return;
    }
    material_mask *mask = source.mask;
    unsigned char *before = NULL;
    if (source.retain_chips) {
        asetlen(d->before, mask->width * mask->height);
        before = d->before;
        memcpy(before, mask->cells, (size_t)mask->width * mask->height);
    }
    int removed = 0;
    float c = cosf(source.angle), s = sinf(source.angle);
    for (int i = index; i < acount(d->damage); i++) {
        material_damage *next = &d->damage[i];
        if (next->target != damage.target) {
            continue;
        }
        float x = next->point.x - source.position.x,
              y = next->point.y - source.position.y;
        float energy = next->energy * (next->cause == MATERIAL_DAMAGE_BLAST
                                           ? source.blast_multiplier
                                           : 1);
        int cut_cells = material_mask_apply_damage(
            mask,
            c * x + s * y,
            -s * x + c * y,
            next->radius,
            energy,
            source.hardness
        );
        material_damage cut = *next;
        next->energy = 0;
        removed += cut_cells;
        if (cut_cells && host->cut_surface) {
            host->cut_surface(host->user, &cut);
        }
    }
    if (!removed) {
        return;
    }
    int dust = removed;
    bool blast = damage.cause == MATERIAL_DAMAGE_BLAST;
    material_mask *chip_pieces = NULL;
    if (before) {
        for (int i = 0; i < mask->width * mask->height; i++) {
            before[i] &= !mask->cells[i];
        }
        material_mask chip = material_mask_copy_cells(mask->width, mask->height, mask->cell_size, before);
        int discarded;
        chip_pieces = material_mask_detach_fragments(&chip, source.minimum, false, &discarded, d->work);
        material_mask_rebuild_work(&chip, d->work);
        apush(chip_pieces, chip);
    }
    int discarded, previous = mask->count;
    material_mask *pieces = material_mask_detach_fragments(
        mask,
        source.minimum,
        source.anchored,
        &discarded,
        d->work
    );
    dust += previous - mask->count;
    /* Host fragment callbacks may inspect source geometry. Publish the detached
     * source before callbacks; rebuild again only if capacity restores cells. */
    material_mask_rebuild_work(mask, d->work);
    fragment_result detached = fragments(
        d,
        host,
        pieces,
        mask,
        true,
        damage,
        blast ? source.blast_speed : source.fragment_speed,
        source.minimum,
        capacity
    );
    dust -= detached.emitted + detached.restored;
    if (chip_pieces) {
        fragment_result chip_result = fragments(
            d,
            host,
            chip_pieces,
            NULL,
            false,
            damage,
            blast ? source.blast_speed : source.chip_speed,
            source.minimum,
            capacity
        );
        dust -= chip_result.emitted + chip_result.restored;
    }
    if (detached.restored) {
        material_mask_rebuild_work(mask, d->work);
    }
    host->invalidate_geometry(host->user, damage.target);
    publish(
        d,
        host,
        (material_event){ .kind = MATERIAL_EVENT_DAMAGE,
                          .target = damage.target,
                          .point = damage.point,
                          .energy = damage.energy,
                          .cause = damage.cause,
                          .impact = damage.impact,
                          .removed = removed,
                          .dust = dust + (mask->count < source.minimum ? mask->count : 0) }
    );
    if (mask->count < source.minimum) {
        host->destroy_source(host->user, damage.target);
        return;
    }
    if (source.release_fraction > 0 &&
        mask->count < source.initial_cells * source.release_fraction) {
        material_destruction_queue_release(d, damage.target);
    }
}

void material_destruction_step(material_destruction *d, const material_host *host, int capacity)
{
    assert(capacity >= 0 && !d->stepping);
    d->stepping = true;
    material_damage *retry = d->retry;
    d->retry = d->retry_work;
    aclear(d->retry);
    for (int i = 0; i < acount(retry); i++) {
        retry_fragments(d, host, retry[i], &capacity);
    }
    aclear(retry);
    d->retry_work = retry;
    for (int i = 0; i < acount(d->damage); i++) {
        apply_damage(d, host, i, &capacity);
    }
    aclear(d->damage);
    int releases = acount(d->release), deferred = 0;
    for (int i = 0; i < releases; i++) {
        material_source source;
        uint64_t target = d->release[i];
        if (!host->read_source(host->user, target, &source)) {
            continue;
        }
        if (source.mask->count && capacity <= 0) {
            d->release[deferred++] = target;
            continue;
        }
        if (source.mask->count) {
            material_mask mask = *source.mask;
            *source.mask = (material_mask){ 0 };
            capacity--;
            host->create_fragment(
                host->user,
                target,
                (material_fragment){ mask, source.position, source.release_speed }
            );
        }
        publish(
            d,
            host,
            (material_event){ .kind = MATERIAL_EVENT_RELEASE,
                              .target = target,
                              .point = source.position }
        );
        host->destroy_source(host->user, target);
    }
    /* Callbacks may append releases. Keep their untouched tail after the
     * deferred originals, preserving both groups' order for the next step. */
    int appended = acount(d->release) - releases;
    if (appended) {
        memmove(
            d->release + deferred,
            d->release + releases,
            (size_t)appended * sizeof *d->release
        );
    }
    asetlen(d->release, deferred + appended);
    d->stepping = false;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t material_destruction_hash(const material_destruction *d, uint64_t hash)
{
    for (int i = 0; i < acount(d->damage); i++) {
        material_damage v = d->damage[i];
        float values[] = { v.point.x, v.point.y, v.radius, v.energy };
        hash = hash_bytes(hash, &v.target, sizeof v.target);
        hash = hash_bytes(hash, &v.cause, sizeof v.cause);
        hash = hash_bytes(hash, values, sizeof values);
    }
    hash = hash_bytes(hash, d->release, (size_t)acount(d->release) * sizeof *d->release);
    for (int i = 0; i < acount(d->retry); i++) {
        material_damage v = d->retry[i];
        float values[] = { v.point.x, v.point.y, v.radius, v.energy };
        hash = hash_bytes(hash, &v.target, sizeof v.target);
        hash = hash_bytes(hash, &v.cause, sizeof v.cause);
        hash = hash_bytes(hash, values, sizeof values);
    }
    for (int i = d->event_cursor; i < acount(d->events); i++) {
        material_event v = d->events[i];
        int values[] = { v.kind, v.cause, v.removed, v.dust };
        float scalars[] = { v.point.x, v.point.y, v.energy };
        hash = hash_bytes(hash, &v.target, sizeof v.target);
        hash = hash_bytes(hash, values, sizeof values);
        hash = hash_bytes(hash, scalars, sizeof scalars);
    }
    return hash;
}
