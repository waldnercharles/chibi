# UE 5.8 Event Graph parity audit and implementation order

Status: reconciled planning record, non-normative  
Audited: 2026-08-19  
Reconciled into spec and roadmap: 2026-08-19  
Reference release: Unreal Engine 5.8

This document records the audit of the former parity claims in `docs/spec.md`,
separates fixed Blueprint language constructs from generated engine actions,
and explains the reordered work. The append-only requirement withdrawals and
replacements are now in the spec under ADR 0007. The spec and roadmap are the
normative and scheduling sources when wording differs from this record.

## Parity definition

The target is user-visible Blueprint language capability, not a copy of every
node available in an Unreal installation.

Epic's Blueprint API reference says that it strives to reflect all available
nodes but is not guaranteed to be exhaustive. An exact node-count target is
therefore neither verifiable nor stable. Unreal also generates graph actions
from reflected functions, properties, structs, enums, events, subsystems,
async factories, loaded modules, and plugins.

Parity is tracked in three lanes:

1. Core language constructs have engine-independent semantics and ship in
   c-blueprints.
2. Generated constructs come from annotated C declarations through
   `reflect.c`.
3. Consumer constructs depend on a host object model, ECS, asset system,
   renderer, input stack, networking layer, or another engine service.

A family is complete only when the parity matrix marks it as one of:

- Core: implemented and tested in c-blueprints.
- Generated: expressible through a tested reflection contract.
- Consumer: deliberately supplied by a host node pack.
- Deferred: named here with a decision gate and planned section.
- Excluded: outside the language target with a stated reason.

"Every catalog entry currently listed has an implementation" is a valid local
claim. "The catalog matches Unreal" is not valid until the matrix closes.

## Reconciled completeness findings

The table describes the pre-reconciliation spec. Every required action is now
either an append-only replacement requirement or an explicit decision gate.

| Area | Former claim or implication | UE 5.8 evidence | Reconciliation |
| --- | --- | --- | --- |
| `CAT-1` | The fixed catalog can represent every relevant node. | Reflected functions and registered actions make the menu open-ended. Epic says its own API list is not guaranteed exhaustive. | Replace fixed-node completeness with the three-lane capability matrix. |
| `CAT-13` to `CAT-15` | Int, String, and Enum are the complete switch family. | Epic's Flow Control page lists Int, String, Name, and Enum. | Add Switch on Name and first-class Name semantics. |
| `CAT-28` | Bool, Byte, Integer, Integer64, Real, and String match Unreal's literal set, with no input pins. | UE 5.8 exposes Make Literal Name. Make Literal Name, String, and Float have a `Value` input and a return output. | Withdraw `CAT-28`; replace it with the sourced set and pin shape. |
| `TYPE-1` | The primitive list is sufficient for the UE-facing language. | UE variables distinguish Name, String, and localized Text. | Add core Name. Keep Text as a registered consumer value until localization semantics exist. |
| `VAR-9` and `CAT-24` | The listed collection operations are complete. | Array nodes also include separate Remove Index and Remove Item, Find, Last Index, Resize, and Set Array Elem. Current Set and Map lists are also partial. | Specify each container operation explicitly rather than referring to a summary. |
| `CAT-17` | A generic IsValid can accept an unspecified value type. | Unreal validity and casts operate on object/class references and can be pure or impure. | Define validity through a handle-type contract or leave it to consumer-generated nodes. |
| `CAT-21` and `CAT-22` | Literals plus Make/Break cover struct authoring. | UE also has Make Array/Set/Map and Set Members in Struct behavior. | Add container constructors and Set Members in Struct to the matrix. |
| `CAT-23` | Reflection can expose every reflected call, pure or impure. | Current `reflect.c` accepts only non-void functions, ordinary value inputs, and one return, then emits every call as pure. | Land reflection v2 before relying on generated-call completeness. |
| `NODE-6` | Every impure node has an exec input. | Event entries are impure execution roots with an exec output and no exec input. | Replace the definition with executable-node semantics that include entries. |
| `EVT-8` | Begin and Tick belong to engine-independent core. | BeginPlay and Tick are host lifecycle events in Unreal. | Core supplies generic named events. The demo host dispatches Begin, Tick, and Input by convention. |
| `VAR-8` and `EXEC-13` | Variables and stateful-node scratch reset for every run. | Persistent Blueprint behavior requires state across separate event activations. | Move both stores onto `blueprint_instance`; keep continuations and temporary data activation-local. |
| Deferred list | Delegates, async actions, timelines, and collapsed graphs can remain deferred while claiming near parity. | UE has dedicated delegate, async-action, timeline, composite, and tunnel node families. | Keep them deferred, but list them as open parity families rather than closed gaps. |

The existing coverage script remains useful. It proves that every current spec
ID is claimed by the roadmap and that completed requirements have evidence. It
does not prove that the spec contains every UE 5.8 language family.

## Node-family inventory

### Core language families

These constructs have useful semantics without a host engine:

| Family | Representative nodes or surfaces | Planned disposition |
| --- | --- | --- |
| Entries and calls | Event, Call Event, Self, Function Entry, Return, function call, macro tunnels | Core |
| Instance state | Variable Get/Set, locals, authored defaults, stateful-node scratch | Core |
| Literals and operators | Bool, Byte, Integer, Integer64, Real, Name, String; promotable arithmetic, comparison, and boolean operators | Core |
| Flow | Branch, Sequence, loops, Switch, Select, DoOnce, DoN, FlipFlop, Gate, MultiGate | Core |
| Latent flow | Delay, Retriggerable Delay, later async continuation protocol | Core |
| Containers | Make Array/Set/Map and operation families | Core |
| Struct and enum | Make, Break, Set Members, enum literal, split pins | Core plus generated types |
| Communication | Typed delegates and event dispatchers | Core, deferred design |
| Graph structure | Functions, macros, collapsed graphs, reroutes, comments | Core |
| Debug authoring | Breakpoints, watches, diagnostics, call stacks, flow visualization | Core, cross-cutting |

### Reflection-generated families

The node count for these families depends on annotated C declarations:

| Family | Reflection contract |
| --- | --- |
| Pure calls | Explicit `NODE_FN` contract; value inputs; scalar, struct, or explicit out results |
| Impure calls | Explicit `NODE_EXEC` contract; generated exec pins and ordered side effects |
| Services | One service-type declaration; generated service descriptor; runtime instance binding; stable auto-service pin |
| Operators | Operation name mapped to reflected pure function signatures; wildcard node chooses the best signature |
| Types | Struct and enum descriptors, Make/Break nodes, names, formatting, defaults, and editor metadata |
| Handles and Self | Nominal opaque handles; optional owner type on a Blueprint definition; instance-supplied Self |

Properties, host event schemas, async factories, and custom value serializers are
future reflection extensions. They are not part of the minimal reflection-v2
slice.

### Consumer families

These names appear in Unreal's Event Graph but their behavior comes from the
engine rather than the visual language:

- Actor, component, world, subsystem, and asset access.
- Dynamic object/class/interface casts and object validity.
- Input, collision, animation, AI, audio, rendering, and UI actions.
- Replication, authority, RPC, save-game, and platform services.
- ECS creation, component access, queries, and prefab authoring.

A consumer can supply these through reflected functions, service-backed pins,
nominal handles, and hand-authored descriptors. c-blueprints does not assume an
ECS or UObject substitute.

## Locked architecture for the reordered work

### Runtime

- An authored Blueprint definition is shared by persistent
  `blueprint_instance` values.
- Instance variables and stateful-node scratch live on the instance.
- Each event activation owns its continuation stack, temporary arena, memo,
  error, step count, and latent state.
- Several activations may coexist on one instance. The instance scheduler
  executes one activation at a time in deterministic order.
- A definition may declare an optional nominal owner-handle type. The instance
  supplies the matching handle, and Self returns it.

### Reflection v2

- `NODE_FN` remains an explicit pure contract. Purity is not inferred from C
  constness or function bodies.
- `NODE_EXEC` marks a reflected call that requires ordered execution.
- Pointer outputs require an explicit out annotation. Direction is never
  inferred from pointer mutability.
- `reflect.c` generates service descriptors and typed host binding helpers from
  one declaration per service type.
- Each event activation snapshots borrowed service instances. Core does not
  own, retain, or destroy services.

Service-backed input pins always exist in the node model. A connected pin wins.
An unconnected pin is hidden when a matching service is bound and shown when it
is unresolved. Registration changes visibility, not pin identity.

### Types and operators

- `PIN_HANDLE` carries a host-owned 64-bit opaque handle plus a nominal type.
- `PIN_NAME` is a core immutable identifier type. Localized Text remains a
  consumer value type.
- Promotable operator nodes select from a reflect-generated operation table.
- Built-in numeric promotion uses Byte to Integer to Integer64 to Real.
- F32 and F64 remain one Real category, selecting F64 when a registered
  signature requires it. Other C widths need exact signatures or explicit
  promotion rules.

### Events and demo

Core supplies generic named Event and Call Event nodes with authored parameter
signatures. It assigns no lifecycle meaning to event names.

The demo host dispatches:

```text
Begin()
Tick(Real DeltaSeconds)
Input(Name Action, Real Value, Bool Pressed)
```

These events verify dispatch, persistent state, parameter values, overlapping
activations, Name, Real, Bool, and reflected host calls.

## Prioritized implementation sections

Every section includes editor authoring, serialization, compile diagnostics,
headless checks, and manual UI evidence. `UI-1` remains a gate.

### Section 0: spec reconciliation (complete)

The replacement requirements and roadmap coverage lists are written. Obsolete
requirements remain in place as WITHDRAWN, and replacements use new IDs under
ADR 0007.

Completed outputs:

- Persistent definition, instance, activation, and scheduler requirements.
- Generic-event replacements for the current hard-coded lifecycle requirement.
- Name, Switch on Name, and corrected literal requirements.
- Reflection-v2 service, handle, pure/impure, and out-parameter requirements.
- A parity-matrix rule that distinguishes Core, Generated, Consumer, Deferred,
  and Excluded families.

### Section 1: event-driven vertical slice

Goal: a consumer can instantiate one Blueprint definition and drive useful
persistent behavior entirely through authored graphs.

Runtime and reflection foundation:

- `blueprint_instance` and activation scheduler.
- Generic event dispatch with parameters.
- Instance variable and node-scratch storage.
- Optional typed Self handle.
- Reflection v2 for pure calls, exec calls, explicit out parameters, generated
  service descriptors, and runtime service bindings.

Node surface:

- Event and Call Event.
- Self and variable Get/Set.
- Bool, Byte, Integer, Integer64, Real, Name, and String literals with Value
  input and return output.
- Promotable Add, Subtract, Multiply, Divide, equality, ordering, And, Or, and
  Not through the reflected operation table.
- Branch, Sequence, Delay, Print String, and reflected call nodes.

Runtime acceptance proof:

- Begin initializes instance variables exactly once.
- Tick reads and writes the same instance state across frames.
- Input dispatches Name, Real, and Bool values without global state.
- A Delay may suspend while later Tick or Input activations execute.
- Two instances of one definition never share variables, scratch, services, or
  activations.

Generated and authoring proof:

- Pure reflected calls never run from preview as side effects; exec calls never
  run without an exec activation.
- A connected service pin overrides injection; a missing unresolved service
  reports a structured error.
- Save before and after execution is byte-identical for authored data.

### Section 2: functions and multiple graphs

Goal: behavior can be decomposed without growing one Event Graph indefinitely.

Includes:

- Function Entry, Return, pure and impure function-call nodes.
- Per-call local variables and instance-variable access.
- Synchronous calls, recursion limits, call stacks, and Step Over.
- Several open graphs with stable per-graph editor state.
- Collapse selection to function and expand function call.

Latent nodes remain illegal in functions. Macros are not pulled into this
section merely because functions exist.

### Section 3: Arrays and loops

Goal: cover the highest-value collection workflows before requiring hashing
or key eligibility. Array membership operations still require per-type equality,
copy, and destruction callbacks.

The UE 5.8 generic array surface includes:

- Make Array, Get, Set Array Elem, Add, Add Unique, Append, Insert, Clear,
  Contains, Find, Identical, Length, Last Index, Resize, Remove Index, and
  Remove Item.
- Is Empty, Is Not Empty, and Is Valid Index.
- Reverse, Swap, Shuffle, Shuffle from Stream, Random Item, and Random Item
  from Stream. Random operations require an accepted random-service and purity
  policy before implementation.
- Typed Sort for Byte, Integer, Integer64, Real, Name, and String, using the
  registered ordering operation where possible.
- ForEach, ForEach with Break, ForLoop with Break, and WhileLoop, with the
  existing step budget as the loop watchdog.

Actor-class Filter Array is a consumer operation rather than a generic array
primitive. Array wildcard groups resolve element type and reject unresolved
connected pins. Array values have explicit copy and lifetime rules.

### Section 4: delegates and event dispatchers

Placement is locked; internal design is not.

Expected user-visible family:

- Delegate value/signature.
- Create Delegate.
- Bind/Add, Unbind/Remove, Clear, Assign, and Broadcast/Call.
- Authored event-dispatcher fields on persistent instances.

Before this section begins, decide callback identity, weak target lifetime,
invocation ordering, reentrancy, serialization, signature evolution, and the
behavior of destroyed targets.

### Section 5: common language completion

This section closes high-utility fixed families after the earlier foundations.
Data and flow families are:

- Switch on Int, String, Name, and Enum; Select.
- DoOnce, DoN, FlipFlop, Gate, MultiGate, and Retriggerable Delay.
- Struct Make, Break, Set Members, split/recombine, and enum literals.
- Explicit conversions and connection-time insertion.
- Sets and Maps after hash/equality contracts are accepted.

Graph reuse covers macros, macro libraries, wildcard-per-call resolution, and
collapsed graphs.

The exact order inside this section remains open. Each family should be a
separate shippable slice rather than one catalog-completion batch.

### Section 6: advanced parity

These families remain visible in the matrix but require later decisions:

- Async action start, resume, cancel, output-event, and lifetime protocol.
- Timelines, curves, playback control, and authored track data.
- Consumer-defined casts, validity, object/class/interface handles, and soft
  references.
- Localized Text values, formatting history, and serialization.
- Full debugger selection across instances and overlapping activations.

Engine-specific input, networking, animation, AI, UI, asset, and ECS nodes stay
in consumer packs even after this section.

## Deferred decision gates

### Before delegates

Decide whether callbacks target events, functions, or both; whether bindings
are weak; how duplicates and removal compare; and whether Broadcast snapshots
the binding list before invoking it.

### Before Sets and Maps

Define which types are valid keys, how type descriptors provide equality and
hashing, whether floating-point keys are permitted, and how deterministic save
order is maintained.

### Before macros and collapsed graphs

Decide whether macro instances expand into a compile product or execute through
shared graph definitions, how persistent scratch inside a macro is scoped, and
how expansion maps debugger locations back to authored nodes.

### Before async actions and timelines

Define cancellation, host completion callbacks, owner destruction, service
lifetime across suspension, output-event ordering, save behavior, and debugger
presentation.

### Before consumer integration

A consumer decides how Blueprint instances map to its objects or entities, how
prefabs or assets create them, and which generated node packs expose world
state. None of those decisions change core language semantics.

## Source record

### Language and editor behavior

- [Blueprint API reference disclaimer](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI)
- [Event Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/event-graph-in-unreal-engine)
- [Nodes](https://dev.epicgames.com/documentation/en-us/unreal-engine/nodes-in-unreal-engine)
- [Flow Control](https://dev.epicgames.com/documentation/en-us/unreal-engine/flow-control-in-unreal-engine)
- [Blueprint Variables](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-variables-in-unreal-engine)

### Reuse and generated actions

- [Function Calls](https://dev.epicgames.com/documentation/en-us/unreal-engine/function-calls-in-unreal-engine)
- [Macros](https://dev.epicgames.com/documentation/en-us/unreal-engine/macros-in-unreal-engine)
- [Collapsing Graphs](https://dev.epicgames.com/documentation/en-us/unreal-engine/collapsing-graphs-in-unreal-engine)
- [Metadata Specifiers](https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine)
- [Blueprint Function Libraries](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-function-libraries-in-unreal-engine)

### UE implementation families

- [FTypePromotion](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/FTypePromotion)
- [UK2Node_PromotableOperator](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UK2Node_PromotableOperator)
- [UK2Node_DynamicCast](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UK2Node_DynamicCast)
- [UK2Node_AsyncAction](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UK2Node_AsyncAction)
- [UK2Node_GetSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UK2Node_GetSubsystem)

### Exact claim checks

- [Make Literal Name](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Utilities/Name/MakeLiteralName)
- [Make Literal String](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Utilities/String/MakeLiteralString)
- [Make Literal Float](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Math/Float/MakeLiteralFloat)
- [UKismetArrayLibrary](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UKismetArrayLibrary)
