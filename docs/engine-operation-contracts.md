# Checked core engine operations, version 1

This is the implemented contract for the callable `Self`, `Delta Time` and
`Delay` definitions. It is the first bounded slice of verified engine operation
descriptors, not a specification of all host calls or all language semantics.
The definitions in `runtime/core_definition.c` select a closed operation identity.
The shared `SCRIPT_ENGINE_OPERATIONS` rows generate both its versioned catalog
contract and its actual opcode mapping in `execution/operation.h`. Native
definition packs generate
the execution binding; a second producer-supplied binding is rejected.

## Checked declaration and execution boundaries

- Owner is a pure, zero-argument function returning one scalar `ecs.entity`
  handle. It reads the current activation's owner, including after a resume.
- Delta is a pure, zero-argument function returning one scalar `float`. It
  converts the current context's double-precision delta seconds to float.
  Neither context read is a compile-time constant.
- Delay is an action with one execution entry, one scalar double duration,
  one completion outcome and no data result. Its effect is suspension, and it
  is not pure. Existing numeric argument conversions occur before it executes.
- These declarations cannot also describe state, component, aggregate or flow
  access, a receiver, variadic arguments or event sending. Reference/collection
  results, any scalar semantic flags or type parameters, and mismatched physical
  types are rejected.
- Raw native packs must supply the matching opcode and a valid declaration.
  The lowerer validates all known declarations before emitting code, including
  calls rewritten by analysis into state reads or writes, and independently
  checks custom linker bindings. A known declaration
  cannot be replaced with a same-signature generic host callback.

No author-visible syntax or persistence shape changes. Names, display text and
defaults remain authored by the existing typed definition; this contract does
not introduce a new callable spelling.

## Ownership, cost and execution guarantees

The `guarantees` bits are positive proofs for the supported implementation.
An absent bit is unknown. `SCRIPT_OPERATION_UNKNOWN` has no descriptor, even
when an opaque host declaration is labeled pure. Unknown operations keep their
existing checked host/command/query path and access policy; this change does
not certify their effects, enable parallel execution or improve borrowing.

Owner and Delta have only scalar outputs, no borrowed payload or result alias,
no argument retention, and no payload allocation/copy. Ordinary scalar register
or value-slot transfers are not payload copying. They execute on the thread
owning the current activation, and cannot reenter authored execution. These
thread facts do not grant concurrent access to a shared activation or world.

Delay operates on the owning activation thread. It does not guarantee freedom
from authored reentry: an immediately ready command can synchronously execute
its authored completion handler. An
awaited inline delay computes `wake_time = now + duration`: if the result is
less than or equal to `now`, execution continues immediately. Otherwise it
saves live values and continuation state, then returns suspended. A NaN wake
time follows the existing suspended path. Captured values remain owned for the
activation's lifetime; scalar/pointer slots are saved into activation storage.
This is not a zero-copy or no-retention operation.

Instance and non-awaited delay calls retain their existing command lowering.
Command preparation and suspended live-state retention can have allocation and
copy costs, so the public Delay descriptor promises no bound for either. This
slice does not change cancellation, wake ordering, argument evaluation, failure
ordering, or the lifetime rules of the command and task implementations.

## Compatibility and consumers

The capability ABI includes the known operation identity, contract and actual
instruction names, version, effects and guarantee bits. A same-signature opaque
host callback has
a distinct ABI. Opaque capability hashes remain unchanged. Native binding
lookup checks the resulting ABI, and incremental source catalog comparison
includes operation identity so compiled callers are not silently reused.

Direct Owner, Delta and awaited Delay calls are already emitted as instructions,
not serialized native imports. Their prepared compatibility continues to use
the instruction mapping/schema identity. Changing instruction payloads or
semantics still requires the manual prepared semantic version guard; this
metadata does not inspect C bodies or replace that obligation. Rebuild prepared
artifacts after a semantic contract change. No project asset migration or
project version bump is required for this additive descriptor.

Global `self`, `delta` and `dt` still have separate typed global bindings. Graph
Self/Delta/Delay nodes consume the shared port definitions but keep their
existing graph intrinsic/pure-kind mapping. This slice does not claim to have
unified either mapping. No editor capability or authoring service is introduced
into a game runtime package: the shared dependency is the neutral capabilities
package, which itself depends only on types.

## Evidence and remaining work

The C `--operations` cases exercise generated bindings; invalid declarations,
duplicate implementations and opcode mismatches; opaque versus known ABI;
incremental source invalidation; custom linker rejection; and native/compiled
execution with zero, negative, positive and NaN durations, scalar and string
values surviving suspension. Existing program, incremental, native catalog and
prepared checks remain applicable. Execution uses a synthetic clock.
The [validation report](engine-operation-validation.md) records the reviewed
implementation, bounded positive matrix, isolated controls and coverage limits.

Remaining L16 work includes verified generic host/capability ownership and
effects, retained/asynchronous argument contracts, per-argument alias/copy
metadata, supported typed native/Wasm call lowering, and contracts for actual
consumer operations. Those require implementation audits and adversarial
fixtures of their own; no purity whitelist or generic registration framework
is established here. Global and graph mapping unification is separate work.
