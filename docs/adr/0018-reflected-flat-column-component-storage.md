# ADR 0018: Reflected flat-column component storage

Status: Accepted
Date: 2026-08-22

## Context

ADR 0017 places durable per-entity state in components. Those components are
addressed from two directions with opposite access patterns. Generic graph code
resolves a component by runtime id and reads one entity's value, which is random
access repeated across thousands of independent activations. Hand-written C
systems iterate one or two components over many entities, which wants dense
contiguous memory.

The graph direction imposes a hard constraint. A `Get Component` node callback
is generic code holding a `type_desc` and a component id; it cannot expand a
compile-time token such as `ECS_MASK(Transform)`. Components must therefore have
runtime identities, which rules out an X-macro component table as the addressing
mechanism regardless of its other merits.

Three prior single-header designs were reviewed. `arch_ecs` groups entities into
archetypes with contiguous columns, which iterates best, but uses compile-time
masks, forbids structural change outright, and expresses iteration through a
deferred-expansion macro that is not maintainable. `fat_ecs` uses flat arrays
sized to a fixed ceiling with a per-entity mask, which gives the simplest and
fastest random access, but embeds one pointer per component in the entity handle
(roughly 336 bytes at 40 components, rebuilt per entity visited) and scans to a
high-water mark on every query. `brutal_ecs` and its `pico_ecs` ancestor use
runtime component ids over paged blocks with per-system sparse sets, which is
the right identity model but reaches component values through a gather and ships
a system scheduler this architecture does not use.

## Decision

Component values live in one flat column per component type, sized to a fixed
entity ceiling, addressed as `base + id * stride`. Random access from generic
graph code is two loads and a multiply, with no archetype, page, or sparse-set
indirection. Addresses are stable for the world's lifetime.

The entity handle is a thin `(id, gen)` pair. Typed access is through generated
accessors rather than pointers carried in the handle.

The iteration index is a separate structure over that storage, so it can be
replaced without touching any accessor, node descriptor, or generated code. The
default index is an occupancy bitset per component plus lowest-index-first slot
allocation, which keeps the high-water mark near the live count. A component MAY
declare a dense entity list instead, through a flag in its annotation, when it is
both rare and hot-iterated.

`reflect/reflect.c` generates the component registry from annotated structs: id,
name, size, stride, column base, `type_desc`, and a layout hash over the field
names, offsets, sizes, and pin types. From the same declaration it generates
typed accessors for C systems and `Get`, `Set`, `Has`, `Add`, and `Remove` nodes
for graphs. There is no hand-maintained component table.

Serialization and inspection are generic. `type_desc` supplies field names,
offsets, and pin types, and `pin_write_field` and `pin_read_field` remain the
only functions that touch struct bytes, per `TYPE-3`. No component carries
per-component serialization code.

A prototype is a data document listing component names with authored field
values and the graph assets to attach. A prototype is not an entity and never
occupies an entity slot.

Structural changes (create, destroy, add component, remove component) are
recorded in a command buffer and applied at a sync point in deterministic sort
key order, never immediately during iteration.

The entity ceiling is a build-time configuration constant, not a property of
the design. Its default is 10,000 graph-addressable entities, chosen because it
is where per-entity Blueprint instances stay affordable rather than because the
storage model requires it. Raising it is supported and its costs are stated
below; every structure that scales with the ceiling is sized from that one
constant.

Mass-spawned visual effects such as particles, debris, and projectiles live in
dedicated pools outside the ECS by default, so a visual-density decision cannot
consume the behavior budget. A consumer that wants them as entities raises the
ceiling instead, and pays the costs below rather than hitting a wall.

`ecs/` depends on `core/` and libc only, and is testable with no window.

## Consequences

At 40 components and the default ceiling, columns cost roughly 10 MB in one
static allocation, occupancy bitsets cost about 50 KB in total, and a
three-component query touches 471 word loads rather than 10,000 mask reads.

Costs scale linearly in the ceiling and are dominated by the columns, so the
decision to raise it is an arithmetic one rather than a redesign. At 100,000 the
columns cost roughly 100 MB and bitsets roughly 500 KB. Two things degrade
faster than linearly in practice: an occupancy scan for a rare component reads
the whole bitset regardless of how few bits are set, and the high-water-mark
effect below grows with the range. Both are addressed by switching that
component's iteration index rather than by changing storage.

Pointer stability is permanent, which makes parallel iteration nearly free to
add: slicing by index range gives each worker a contiguous disjoint region of
every column, so workers never write the same cache line.

Adding or removing a component is a bit flip plus a value write, with no row
relocation and no dense-list maintenance. This matters because ADR 0017 makes
structural change a normal way for behavior to express itself.

The high-water mark never shrinks below the highest live slot, so a world that
spawns 10,000 and settles at 100 continues to scan a larger bitset than it needs
until slots at the tail free. At this ceiling the cost is measured in
microseconds; lowest-index-first allocation keeps it bounded in practice.

The two index strategies trade against each other along density rather than
scale. A dense list costs a sparse index per component sized to the ceiling, so
at one million slots dense lists across 40 components cost roughly 160 MB
against 5 MB of bitsets, while a bitset scan for a component held by a handful
of entities reads the full range to find them. Bitsets win on memory and on
structural-change cost; dense lists win on iterating rare components. Because
the iteration index is separate from storage, this is per-component policy set
by an annotation flag, and the right default may differ at a different ceiling.
Changing it touches no accessor, no node descriptor, and no generated code.

The generated layout hash gives C hot-reloading a precise tripwire: a reload
knows exactly which components changed shape, and because both the old and new
`type_desc` are available, a changed column can be migrated by field name rather
than reinitialized.

## Alternatives rejected

### Archetype chunks

Contiguous columns per archetype iterate multi-component queries best and are
the strongest choice when a query names four or more components. The graph
workload does not do that; it does single-entity random access, where archetypes
add an entity-to-archetype-and-row lookup and gain nothing. Adding or removing a
component relocates the entity's row, which is real machinery serving an access
pattern that is not the dominant one here.

### Entity-indexed paged blocks with per-system sparse sets

The `brutal_ecs` and `pico_ecs` model has the correct runtime identity semantics
and reaches every value through a page lookup or a gather. It also ships a system
scheduler with its own groups and parallel task callbacks, which would compete
with the activation scheduler this architecture already has. Adopting it whole
means importing roughly 1300 lines to avoid writing 500 and taking an indirection
on the hottest path to do it.

### Compile-time X-macro component table

Unusable from generic node callbacks, which is decisive. It also has to be
maintained by hand, which is the reason it existed before reflection worked.

### Prototypes as entities

Storing templates as live entities requires excluding them from every query,
which `arch_ecs` did with a per-entity `excluded` flag consulted in the inner
loop. Prototypes as data documents remove the flag and the ceiling that came
with them.

### Immediate structural change

Applying creates and destroys during iteration invalidates the iteration itself
and makes parallel execution unsound. Deferral is required by ADR 0019 in any
case, so a second immediate path would only be a second way to be wrong.
