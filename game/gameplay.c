#include "gameplay.h"

#include "../core/array.h"
#include "../core/xalloc.h"
#include "entities.h"
#include "generated.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

typedef struct
{
    ecs_entity entity;
    GameTransform transform;
    GameCollider collider;
    double lower;
    double upper;
    double cross_lower;
    double cross_upper;
} contact_body;

typedef struct
{
    uint64_t first;
    uint64_t second;
} contact_pair;

struct game_contacts
{
    ecs_world *world;
    game_entities *entities;
    contact_pair *previous;
};

game_contacts *game_contacts_new(ecs_world *world, game_entities *entities)
{
    assert(world && entities);
    game_contacts *contacts = xcalloc(1, sizeof *contacts);
    contacts->world = world;
    contacts->entities = entities;
    return contacts;
}

void game_contacts_reset(game_contacts *contacts)
{
    asetlen(contacts->previous, 0);
}

void game_contacts_free(game_contacts *contacts)
{
    afree(contacts->previous);
    free(contacts);
}

static int compare_bodies(const void *left, const void *right)
{
    const contact_body *a = left;
    const contact_body *b = right;
    if (a->lower != b->lower) {
        return (a->lower > b->lower) - (a->lower < b->lower);
    }
    uint64_t first = ecs_entity_to_u64(a->entity);
    uint64_t second = ecs_entity_to_u64(b->entity);
    return (first > second) - (first < second);
}

static bool valid_body(const contact_body *body)
{
    return isfinite(body->transform.x) && isfinite(body->transform.y) &&
           isfinite(body->collider.radius) && body->collider.radius >= 0 &&
           body->collider.category && body->collider.mask;
}

static void sort_bodies(contact_body *bodies)
{
    if (acount(bodies) < 2) {
        return;
    }
    double min_x = bodies[0].transform.x, max_x = min_x;
    double min_y = bodies[0].transform.y, max_y = min_y;
    for (int i = 1; i < acount(bodies); i++) {
        min_x = fmin(min_x, bodies[i].transform.x);
        max_x = fmax(max_x, bodies[i].transform.x);
        min_y = fmin(min_y, bodies[i].transform.y);
        max_y = fmax(max_y, bodies[i].transform.y);
    }
    bool vertical = max_y - min_y > max_x - min_x;
    for (int i = 0; i < acount(bodies); i++) {
        contact_body *body = &bodies[i];
        double center = vertical ? body->transform.y : body->transform.x;
        /* The exact squared-distance test rounds at the scale of its operands.
         * Expand bounds conservatively so cancellation at extreme coordinates
         * cannot discard a pair that that test accepts. */
        double margin = 8 * DBL_EPSILON * (fabs(center) + body->collider.radius);
        body->lower = center - body->collider.radius - margin;
        body->upper = center + body->collider.radius + margin;
        double cross = vertical ? body->transform.x : body->transform.y;
        double cross_margin = 8 * DBL_EPSILON * (fabs(cross) + body->collider.radius);
        body->cross_lower = cross - body->collider.radius - cross_margin;
        body->cross_upper = cross + body->collider.radius + cross_margin;
    }
    qsort(bodies, (size_t)acount(bodies), sizeof *bodies, compare_bodies);
}

static int compare_pairs(const void *left, const void *right)
{
    const contact_pair *a = left;
    const contact_pair *b = right;
    if (a->first != b->first) {
        return (a->first > b->first) - (a->first < b->first);
    }
    return (a->second > b->second) - (a->second < b->second);
}

static bool overlaps(const contact_body *a, const contact_body *b)
{
    if (!(a->collider.category & b->collider.mask) ||
        !(b->collider.category & a->collider.mask) ||
        a->collider.radius < 0.0f || b->collider.radius < 0.0f) {
        return false;
    }
    double x = (double)a->transform.x - b->transform.x;
    double y = (double)a->transform.y - b->transform.y;
    double radius = (double)a->collider.radius + b->collider.radius;
    return isfinite(radius) && x * x + y * y <= radius * radius;
}

static int queue_contact(game_contacts *contacts, uint64_t target, uint64_t other)
{
    pin_value value = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = other };
    return game_entities_request_event(
        contacts->entities,
        ecs_entity_from_u64(target),
        "Contact Begin",
        (flow_inputs){ .values = &value, .count = 1 }
    );
}

int game_contacts_tick(game_contacts *contacts)
{
    ecs_component_id required[] = {
        GAME_COMPONENT_GameTransform.id,
        GAME_COMPONENT_GameCollider.id,
    };
    for (int i = 0; i < 2; i++) {
        if (!ecs_world_component(contacts->world, required[i])) {
            game_contacts_reset(contacts);
            return 0;
        }
    }
    ecs_query query;
    int result = ecs_query_begin(contacts->world, &query, required, 2);
    if (result != ECS_OK) {
        return result;
    }
    contact_body *bodies = NULL;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        contact_body body = {
            .entity = entity,
            .transform = *GAME_GameTransform_get_const(contacts->world, entity),
            .collider = *GAME_GameCollider_get_const(contacts->world, entity),
        };
        if (valid_body(&body)) {
            apush(bodies, body);
        }
    }
    ecs_query_end(&query);
    sort_bodies(bodies);
    contact_pair *current = NULL;
    for (int i = 0; i < acount(bodies); i++) {
        for (int j = i + 1; j < acount(bodies); j++) {
            if (bodies[j].lower > bodies[i].upper) {
                break;
            }
            if (bodies[i].cross_lower > bodies[j].cross_upper ||
                bodies[j].cross_lower > bodies[i].cross_upper) {
                continue;
            }
            if (overlaps(&bodies[i], &bodies[j])) {
                contact_pair pair = {
                    ecs_entity_to_u64(bodies[i].entity),
                    ecs_entity_to_u64(bodies[j].entity),
                };
                if (pair.first > pair.second) {
                    uint64_t swap = pair.first;
                    pair.first = pair.second;
                    pair.second = swap;
                }
                apush(current, pair);
            }
        }
    }
    afree(bodies);
    if (acount(current) > 1) {
        qsort(current, (size_t)acount(current), sizeof *current, compare_pairs);
    }
    for (int i = 0; i < acount(current); i++) {
        contact_pair pair = current[i];
        if (acount(contacts->previous) &&
            bsearch(&pair, contacts->previous, (size_t)acount(contacts->previous), sizeof pair, compare_pairs)) {
            continue;
        }
        int first = queue_contact(contacts, pair.first, pair.second);
        int second = queue_contact(contacts, pair.second, pair.first);
        if (first < 0 || second < 0) {
            result = first < 0 ? first : second;
        }
    }
    afree(contacts->previous);
    contacts->previous = current;
    return result;
}
