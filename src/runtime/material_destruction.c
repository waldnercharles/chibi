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
    uint64_t *release;
    material_event *events;
    int event_cursor;
};

material_destruction *material_destruction_new()
{
    return xcalloc(1, sizeof(material_destruction));
}

void material_destruction_reset(material_destruction *d)
{
    aclear(d->damage);
    aclear(d->release);
    aclear(d->events);
    d->event_cursor = 0;
}

void material_destruction_free(material_destruction *d)
{
    afree(d->damage);
    afree(d->release);
    afree(d->events);
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

static int fragments(
    material_destruction *d,
    const material_host *host,
    material_mask *pieces,
    material_damage damage,
    float speed,
    int minimum,
    int *capacity
)
{
    int retained = 0;
    for (int i = 0; i < acount(pieces); i++) {
        if (pieces[i].count < minimum || *capacity <= 0) {
            material_mask_free(&pieces[i]);
            continue;
        }
        retained += pieces[i].count;
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
    return retained;
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
        before = xmalloc((size_t)mask->width * mask->height);
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
        removed += material_mask_apply_damage(
            mask,
            c * x + s * y,
            -s * x + c * y,
            next->radius,
            energy,
            source.hardness
        );
        next->energy = 0;
    }
    if (!removed) {
        free(before);
        return;
    }
    material_mask_rebuild(mask);
    int dust = removed;
    bool blast = damage.cause == MATERIAL_DAMAGE_BLAST;
    if (before) {
        for (int i = 0; i < mask->width * mask->height; i++) {
            before[i] &= !mask->cells[i];
        }
        material_mask chip = material_mask_from_cells(mask->width, mask->height, mask->cell_size, before);
        free(before);
        int discarded;
        material_mask *pieces = material_mask_extract_fragments(&chip, source.minimum, false, &discarded);
        apush(pieces, chip);
        dust -= fragments(
            d,
            host,
            pieces,
            damage,
            blast ? source.blast_speed : source.chip_speed,
            source.minimum,
            capacity
        );
    }
    int discarded, previous = mask->count;
    material_mask *pieces = material_mask_extract_fragments(
        mask,
        source.minimum,
        source.anchored,
        &discarded
    );
    dust += previous - mask->count;
    dust -= fragments(
        d,
        host,
        pieces,
        damage,
        blast ? source.blast_speed : source.fragment_speed,
        source.minimum,
        capacity
    );
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
    assert(capacity >= 0);
    for (int i = 0; i < acount(d->damage); i++) {
        apply_damage(d, host, i, &capacity);
    }
    aclear(d->damage);
    for (int i = 0; i < acount(d->release); i++) {
        material_source source;
        uint64_t target = d->release[i];
        if (!host->read_source(host->user, target, &source)) {
            continue;
        }
        if (capacity > 0 && source.mask->count) {
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
    aclear(d->release);
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
