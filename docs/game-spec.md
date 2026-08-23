# c-blueprints game runtime specification

Normative description of the game runtime built on c-blueprints: an entity
store, a prototype format, behavior attachment, parallel execution, live
authoring, host surfaces, and module hot reload.

This document is the consumer side of the boundary `docs/spec.md` draws. That
document specifies a visual scripting language that is engine-agnostic and
ECS-agnostic, and states that an ECS binding is a separate optional module
layered on the same node descriptor and reflection APIs, never folded into
core. This is that module. Nothing here may be required by `core/`.

Requirements carry stable IDs under the same rules as `docs/spec.md`, defined
in `docs/adr/0007`: append-only, never renumbered, never reused, withdrawn in
place. `tools/spec_coverage.py` gates this document alongside `docs/spec.md`,
so an ID here with no citing check in `test/self_check.c` is mechanically
visible.

Requirements use RFC 2119 sense: MUST is binding, SHOULD admits a documented
exception, MAY is optional.

Decisions are recorded in `docs/adr/0017` (behavior attachment), `0018`
(component storage), `0019` (parallel activation), and `0020` (live
authoring).

## Contents

- [ECS](#ecs-entity-and-component-storage) entity and component storage
- [PROTO](#proto-prototypes-and-spawning) prototypes and spawning
- [BEH](#beh-behavior-attachment) behavior attachment
- [PAR](#par-declared-effects-and-parallel-execution) declared effects and parallel execution
- [APPLY](#apply-authoring-against-a-live-world) authoring against a live world
- [ENG](#eng-host-surfaces) host surfaces
- [HOT](#hot-module-hot-reload) module hot reload
- [Implementation layout](#implementation-layout)

## ECS: entity and component storage

See `docs/adr/0018`.

`ECS-1` The world MUST have a fixed entity slot count set by one build-time
constant, with a default of 10,000. Every structure that scales with entity
count MUST be sized from that constant, so raising it is a configuration change
rather than a redesign.

`ECS-2` An entity handle MUST be a 64-bit pair of a slot id and a generation.
Slot id zero MUST mean no entity and MUST NOT be allocated.

`ECS-3` Destroying an entity MUST advance its slot generation. A handle to a
destroyed entity MUST resolve to nothing and MUST NOT resolve to a later entity
occupying the same slot.

`ECS-4` A component type MUST be described by a component descriptor giving a
runtime id, a name, an element size, its column base, its `type_desc`, and a
layout hash over its field names, offsets, sizes, and pin types.

`ECS-5` Component values MUST live in one flat column per component type,
addressed as the column base plus the slot id times the element size. A
component value's address MUST remain stable for the world's lifetime.

`ECS-6` Generic code MUST be able to read and write any component given only
its runtime id and its `type_desc`, with no compile-time knowledge of the C
type, because generated node callbacks have no other information.

`ECS-7` Component field access MUST go through `pin_write_field` and
`pin_read_field`, extending `TYPE-3` to component storage. No other code may
compute a component field address.

`ECS-8` Each component type MUST have an occupancy index recording which
entities hold it. The default index is a bitset over entity slots.

`ECS-9` A component type MAY declare a dense entity list as its occupancy index
instead of a bitset. Changing a component's index MUST NOT change any accessor,
node descriptor, generated symbol, prototype document, or observable behavior.

`ECS-10` A query over N component types MUST visit exactly the entities holding
all N. When every named component uses the bitset index, the visit order MUST be
ascending slot id.

`ECS-11` Entity slot allocation MUST prefer the lowest free slot, so the
high-water mark tracks the live count rather than the peak.

`ECS-12` Create, destroy, add component, and remove component MUST be recorded
in a command buffer rather than applied immediately.

`ECS-13` Each recorded command MUST carry a sort key. Applying the buffer MUST
process commands in sort key order, so the resulting world is identical
regardless of the order in which concurrent producers recorded them.

`ECS-14` The command buffer MUST be applied at a defined sync point, never
during iteration.

`ECS-15` A destroyed entity's component values MUST remain readable until the
buffer is applied, so a death handler can read what died.

`ECS-16` Adding a component an entity already holds, removing one it does not
hold, or addressing a destroyed entity MUST report an error rather than
silently succeeding.

`ECS-17` The ECS module MUST NOT include any host, windowing, rendering, or
editor header, and every behavior in this section MUST be testable without
creating a window.

`ECS-18` The world MUST expose a deterministic hash over every live entity and
component value, so determinism can be asserted between runs and between thread
counts.

## PROTO: prototypes and spawning

`PROTO-1` A prototype is a data document naming component types with authored
field values and naming the graph assets to attach. It is the unit of
data-driven entity authoring.

`PROTO-2` A prototype MUST NOT occupy an entity slot and MUST NOT be visible to
any query.

`PROTO-3` Spawning from a prototype MUST allocate an entity, write each named
component's authored field values through `pin_write_field`, and create one
Blueprint instance per attached graph asset.

`PROTO-4` Prototype load and save MUST be generic over component descriptors.
No component may require per-component serialization code.

`PROTO-5` A prototype naming a component type, field, or graph asset that is
unavailable in the active registry MUST fail with a message naming the missing
entity, and MUST NOT spawn a partially initialized entity.

`PROTO-6` A prototype MAY include another prototype. An included value MUST be
overridable by the including document, and an include cycle MUST be an error.

`PROTO-7` Prototype output MUST be deterministic in key order and number
formatting, so files diff cleanly.

`PROTO-8` Editing a prototype document MUST NOT retroactively change entities
already spawned from it.

## BEH: behavior attachment

See `docs/adr/0017`.

`BEH-1` An entity MAY have any number of attached Blueprint instances, one per
graph asset named by its prototype.

`BEH-2` Instances attached to one entity MUST be independent runtime state
under `RUN-18`, sharing only that entity's components.

`BEH-3` An instance attached to an entity MUST carry that entity as its owner
handle under `BP-12`, so `Self` returns it and `CAT-32` resolves.

`BEH-4` A Blueprint instance's variables are private to that instance. No other
instance may read or write them.

`BEH-5` Per-entity state observed by another graph, a C system, rendering, or
serialization MUST be a component rather than an instance variable. Instance
variables hold behavior-local transient state only.

`BEH-6` The editor MUST offer promoting an instance variable to a component:
it creates the component type from the variable's type, rewires that variable's
Get and Set nodes to component access, and removes the variable.

`BEH-7` Destroying an entity MUST destroy every instance attached to it under
`RUN-15`, cancelling their activations before its component storage is
released.

`BEH-8` Dispatching a named event to an entity MUST dispatch to every attached
instance that declares that event, in a deterministic order, and MUST be a
no-op for attached instances that do not declare it.

`BEH-9` An attached instance whose graph declares no handler for a pumped event
MUST cost nothing on that pump.

`BEH-10` Blueprint instance storage MUST be pooled, so creating and destroying
instances performs no per-instance allocation in steady state.

`BEH-11` Behavior that requires a continuation MUST use a per-entity instance.
Uniform per-frame computation over many entities MAY instead be a C system or
one batch graph, and that choice MUST NOT change observable component values.

## PAR: declared effects and parallel execution

See `docs/adr/0019`.

`PAR-1` A node descriptor MUST be able to declare its access: the component
types it reads, the component types it writes, and flags for structural change,
world read, host sink output, and purity.

`PAR-2` A node descriptor with no access declaration MUST be treated as writing
every component type and as unsafe to run concurrently. Parallel eligibility is
opt-in.

`PAR-3` Reflection MUST carry an access declaration from its annotation to the
generated descriptor, so a consumer declares effects without modifying `core/`.

`PAR-4` The compile pass MUST union per-node access into a per-entry-point
effect mask, cached against the blueprint's topology revision and recomputed
when that revision moves.

`PAR-5` A write is entity-scoped when its target is the writing instance's
owner entity. Two activations whose writes are all entity-scoped and whose
owner entities differ MUST be eligible to run concurrently.

`PAR-6` Two activations on one Blueprint instance MUST NOT run concurrently.

`PAR-7` An effect that is not entity-scoped MUST be recorded in the command
buffer under `ECS-12` rather than performed during the activation. This covers
writes to another entity, structural change, delegate dispatch, and calls to
any service not declared thread-safe.

`PAR-8` Core MUST NOT include a thread pool. Parallel execution MUST be driven
by a host-supplied callback carried in the run options, so `SCOPE-1` holds and
headless tests run single-threaded.

`PAR-9` Any enabled breakpoint MUST force single-threaded execution for that
world.

`PAR-10` A seeded world advanced a fixed number of frames MUST produce an
identical per-frame hash under `ECS-18` at any thread count.

`PAR-11` The compile pass MUST report which entry points are ineligible for
concurrent execution, naming the node and the declared effect responsible.

## APPLY: authoring against a live world

See `docs/adr/0020`.

`APPLY-1` A Blueprint definition exists as an authored working copy and as an
applied snapshot. The editor mutates the working copy; a live world runs the
snapshot.

`APPLY-2` A working copy MUST have no attached instances. Authoring against it
MUST NOT be rejected and MUST NOT require any instance teardown.

`APPLY-3` An applied snapshot MUST be immutable while any instance is attached
to it.

`APPLY-4` Applying MUST, as one operation, destroy every instance of the
changed definition, replace the snapshot, and recreate those instances.

`APPLY-5` Applying MUST be scoped to the changed definition. Instances of
unchanged definitions, and every entity's components, MUST be unaffected.

`APPLY-6` Applying MUST preserve node, pin, graph, and variable identities
under `ID-1`, so breakpoints, watches, and diagnostics map across it.

`APPLY-7` Applying MUST be an explicit user action. Compile is that action when
a world is live.

`APPLY-8` The editor MUST show when a definition has edits that have not been
applied, so what is displayed cannot silently differ from what is running.

`APPLY-9` Undo and redo MUST apply to the working copy, and runtime state MUST
NOT enter the undo history.

`APPLY-10` Applying MAY carry instance state across when node identities are
unchanged. Doing so MUST NOT change any observable result other than the
preserved state.

`APPLY-11` An instance recreated by applying MUST re-run its `Begin` event.

## ENG: host surfaces

`ENG-1` The scene MUST render to an offscreen target displayed in a dockable
editor panel, so the scene view and graph view coexist under `UI-42`.

`ENG-2` The viewport panel MUST size its render target to the panel, and MUST
NOT recreate that target on every frame while the panel is being resized.

`ENG-3` Host input MUST be routed to exactly one consumer: a focused viewport,
a focused text field, or the graph canvas. Focus MUST be visually distinct and
Escape MUST release it, extending `UI-52` from text fields to viewports.

`ENG-4` Player input MUST reach graphs as a dispatched event with typed
parameters under `EVT-9`, never through a bespoke path that bypasses event
dispatch.

`ENG-5` A headless world MUST run with no engine module present, so every
requirement outside this section is testable without a window.

`ENG-6` The frame loop MUST advance the world with a host monotonic time under
`EXEC-10` and `RUN-10`, and MUST apply the command buffer exactly once per
frame at a defined point in that order.

`ENG-7` Frame timing MUST be separately reportable for behavior, systems, and
rendering, so a regression can be attributed.

## HOT: module hot reload

`HOT-1` The game module MAY be loaded as a shared library and reloaded when its
file changes. Core, the editor, and the host application MUST remain loaded
across a reload.

`HOT-2` A reload MUST re-resolve every node's descriptor by descriptor name
under `FMT-5`. No descriptor pointer may survive a reload.

`HOT-3` A reload MUST NOT require unwinding an activation. A suspended
activation MUST survive a reload, which `EXEC-2` makes possible by keeping
continuations off the C call stack.

`HOT-4` A reload MUST compare each component's layout hash under `ECS-4` and
MUST report every component whose layout changed.

`HOT-5` A component whose layout changed MUST be migrated field by field by
name where both layouts name a field with a compatible pin type. Fields with no
counterpart MUST be set to the typed zero value.

`HOT-6` A reload that fails MUST leave the previously loaded module running and
MUST report the failure rather than leaving the world without behavior.

`HOT-7` A release build MUST be able to link the game module statically, with
no reload path compiled in.

## Implementation layout

`ecs/` holds entity slots, component columns, occupancy indices, the command
buffer, and the world hash. Depends on `core/` and libc only. No window.

`engine/` holds rendering, the viewport panel, input routing, the frame loop,
and the module loader. Depends on `core/`, `ecs/`, and the host.

`game/` holds annotated component structs, annotated node packs, C systems,
prototype documents, and graph assets. Its reflection pack is composed the way
an external consumer's would be.

`core/` includes no header from any of the above, per `SCOPE-1`.
