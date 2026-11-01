# Behaviors, components, systems and queries (archived)

Archived 2026-10-31 from `docs/code-language.md`. The compiler rejects these forms with an "are archived" diagnostic. `library` blocks, tasks and input actions stay.

## Behaviors and state

A behavior reacts to events on an entity. `self` names that entity.
Declarations at the behavior root persist between events.

```chibi
Lifetime :: behavior {
    elapsed: number = 0.0
    lifetime: number = 3.0

    on Tick :: (dt: s) {
        elapsed += dt
        if elapsed >= lifetime {
            destroy_entity(self)
        }
    }
}
```

A handler lists the payload fields that it reads, with their types. Fields
match by name, so a handler can list a subset in any order. A handler that
reads no payload fields writes `()`.

```chibi
Timer :: behavior {
    elapsed: number = 0.0

    on Begin :: () {
        elapsed = 0.0
    }

    on Tick :: (dt: s) {
        elapsed += dt
    }
}
```

A handler cannot read unlisted payload fields or `event`. The handler can change a payload parameter only when the event declares a
reference. A written unit must match
the payload field's unit.

`emit` sends an event to one entity. The target comes first, as the receiver
of a method call; `emit Damage(target, amount, self)` means the same as
`emit target.Damage(amount, self)`. `await emit` waits for the target's
handlers.

```chibi
Spikes :: behavior {
    target: entity = 0

    on Begin :: () {
        emit target.Damage(1, self)
    }
}
```

An explicit `retain` binding keeps private state in the enclosing behavior
attachment or system registration. A helper call does not create a new owner.

```chibi
Counter :: behavior {
    visits: retain int = 0
}
```

Use components for entity-visible state. Use invocation locals for temporary
work. All matches of one system share its retained state.

Live replacement preserves matching retained values, and new bindings use
their defaults. A reload always applies. A retained value whose type changed
is converted: record fields match by name, new fields and fields that cannot
convert take their defaults, removed fields are dropped, and numbers convert
as `T(value)` does, even when that loses precision. Items of an array convert
the same way, except that they cannot take a new field's default yet. A
retained value that cannot be converted, or that keeps a pointer to a record
whose layout changed, restarts from its default. Replacement cancels old
continuations and tasks. An explicit restart resets all of the owner's
retained state.

## Components and systems

A component declares data attached to an entity.

```chibi
Health :: component {
    current: int
    maximum: int
}
```

Paths read and write reflected component fields. A component path used as a
condition tests whether the component exists.

```chibi
Damage :: behavior {
    on Begin :: () {
        if self.Health && self.Health.current > 0 {
            self.Health.current -= 1
        }
    }
}
```

A system runs its body for each matching query row. Its flat header declares
component bindings, relationship generators, computed values and `if` filters.
A component value is a read-only snapshot for that row; a pointer binding
accesses the stored component. `?` makes a component binding optional.

```chibi
EmitterControl :: system(
    emitter: ^ParticleEmitter,
    trigger: Aim?,
    if !Collider,
) {
    if trigger {
        emitter.Active = trigger.Firing
    }
    emitter.Rate += dt
}
```

Every system runs on Tick and has a read-only local `dt` in seconds. Pass it
explicitly to helpers that need it. Behavior event handlers continue to list
the payload fields they read.
An empty `system()` header keeps manual registration: it runs for its attached
entity and does not create a world query.

`in` generates one row for each relationship target. `from` binds a component
on a source already in the row. A required component excludes rows where it
is absent. A computed binding uses ordinary `:=` inference or an explicit
type and initializer. Header expressions are pure and cannot suspend.
Pass values explicitly to header helpers, such as `Alive(health.current)`;
helpers cannot implicitly capture the header's bindings or enclosing locals.

```chibi
DamageTargets :: system(
    damage: Damage,
    target: entity in self.Targets,
    health: ^Health from target,
    if health.current > 0,
    amount := damage.amount,
) {
    health.current -= amount
}
```

Names become available from left to right. A failed `if` skips the remaining
header entries for that row. Assigning a field of a component value is an
error; use a pointer binding when the component should change. Pointer fields
inside a copied value keep their ordinary shared-target semantics.

The same description can be used explicitly as an iterable query:

```chibi
Combat :: behavior {
    ApplyDamage :: () {
        matches := query(
            damage: Damage,
            target in self.Targets,
            health: ^Health from target,
            if health.current > 0,
        )
        for row in matches {
            row.health.current -= row.damage.amount
        }
    }
}
```

Query descriptions capture outer values when constructed. Each loop has its
own iteration state and releases it when the loop completes or exits.
Component references resolve current storage while their original component
incarnation exists. Removing the component or destroying its entity or world
permanently invalidates the reference; later access fails with a runtime
diagnostic. Re-adding a component creates a new identity. An existing reference
never reattaches to a reused entity slot.
A `from` binding selects its component when the row is admitted. Changing
the relationship during a suspended invocation does not retarget its pointer;
the next invocation binds the new target.

System-body locals belong to each complete root/target tuple. They survive
suspension until that invocation completes. Entity state belongs in components
when it must outlive the invocation. Structural tuple loss cancels that tuple's
work; failing a value filter does not cancel work that is already suspended.

A reduction collects contributions to a numeric component field. It publishes
them after the system batch completes.

```chibi
ApplyDamage :: system(projectile: Projectile) {
    reduce projectile.Target.Health.current += -projectile.Damage
}
```

A reduction producer cannot otherwise read or write the reduced component.
Ordinary reductions replay contributions in stable system/query order.

## Input and named entities

Input actions come from the project's input map. `.held` stays true while a
source is down. `.pressed` is true for the fixed step after a press.

```chibi
BoostEmitter :: behavior {
    on Tick :: () {
        self.ParticleEmitter.Active = input.Boost.held
    }
}
```

`globals` exposes scene entities with authored global names. An unknown name is
a source error. A declared name absent from the current scene is a runtime error.

```chibi
CameraFollow :: behavior {
    on Tick :: () {
        self.Transform = globals.Player.Transform
    }
}
```

A global member is a read-only entity reference. Its writable components can
still change. Assigning a global entity to a variable captures its current
handle rather than a live binding.

A root-level `bind` maintains a component field as its sources change.

```chibi
LinkedEmitter :: behavior {
    bind self.ParticleEmitter.Active = input.Boost.held
}
```
