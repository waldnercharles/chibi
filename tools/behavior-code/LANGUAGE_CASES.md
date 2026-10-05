# Behavior language acceptance cases

The named alias/event resolver and state array/set lowering cases covered by `behavior-code.test.mjs` are implemented. Native graph publication accepts the lifecycle, collection-loop, and entity-set examples. Each negative compile case must leave both the behavior asset and source map unchanged.

The resolver suite may register test-only components: `NameCollisionFixture` with settable `float dt` and `float age`, and `ReadOnlyFixture` with readable but non-settable `float value`. They are not game components.

## Positive cases

### `single-component-and-event`

```c
behavior ProjectileLifecycle {
  on Tick(GameProjectile projectile) {
    projectile.age += event.dt;
    if (projectile.age >= projectile.lifetime) {
      destroy(self);
    }
  }
}
```

Expect `projectile.age` and `projectile.lifetime` to resolve against `GAME.GameProjectile`; `event.dt` to resolve to Tick's `DeltaSeconds` (`float`, read-only); and `self` to resolve as the behavior owner's `ecs.entity`. `projectile.age` is writable only if the registry exposes its set-field operation. Lower component fields to ordinary typed field-bound Behavior Expression inputs and `destroy(self)` to a Destroy Entity node.

### `same-field-name-is-qualified`

```c
behavior CompareLifetimes {
  on Tick(GameProjectile projectile, GamePickup pickup) {
    if (projectile.lifetime > pickup.lifetime) {
      destroy(self);
    }
  }
}
```

Expect the left and right `lifetime` references to retain their respective component keys. No global field search or alias shadowing is involved.

### `unique-field-shorthand`

```c
behavior ProjectileLifecycle {
  on Tick(GameProjectile projectile) {
    age += event.dt;
    if (age >= projectile.lifetime) {
      destroy(self);
    }
  }
}
```

Expect bare `age` to resolve to `projectile.age`, since exactly one declared component context exposes `age`. The same unqualified spelling must stop compiling if a second declared context exposes `age`.

### `event-payload-schema`

Resolver assertions (not a full behavior fixture): `event.dt` in Tick resolves to `DeltaSeconds: float`; `event.direction` in Input resolves to `Direction: CF_V2`; and `event.other` / `event.point` in ContactBegin resolve to `Other: ecs.entity` / `Point: CF_V2`. Payload values are read-only. ContactBegin maps to the existing host event label `Contact Begin`.

### `event-name-does-not-shadow-component-field`

```c
behavior SeparateClockAndField {
  on Tick(NameCollisionFixture clock) {
    dt += event.dt;
  }
}
```

Expect bare `dt` to resolve by unique-field shorthand to `NameCollisionFixture.dt`, while qualified `event.dt` resolves to the Tick payload. Neither spelling silently shadows the other; bare names never resolve to event payloads.

### `graph-edit-roundtrip`

Compile `single-component-and-event`, edit its comparison and a reflected component-field input binding in the graph editor, then decompile. Expect qualified references and comments to remain readable, the graph edits to appear in source, and compilation of that source to restore the same graph node IDs. Internal field and event pin aliases must reverse to the author-facing component and event paths.

## Negative cases

### `ambiguous-unqualified-field`

```c
behavior CompareAges {
  on Tick(GameProjectile projectile, GamePickup pickup) {
    age += event.dt;
  }
}
```

Expect `E_NAME_AMBIGUOUS_COMPONENT_FIELD` at `age`, suggesting `projectile.age` or `pickup.age`. Do not pick the first declared component.

### `unknown-component-field`

```c
behavior BadField {
  on Tick(GameProjectile projectile) {
    projectile.lifespan += event.dt;
  }
}
```

Expect `E_COMPONENT_FIELD_NOT_FOUND` at `lifespan`; do not reinterpret it as a local or search another component.

### `bare-alias-collides-with-field`

```c
behavior AliasFieldCollision {
  on Tick(GameProjectile damage) {
    damage += event.dt;
  }
}
```

Here `damage` names both the component alias and its reflected `GameProjectile.damage` field. Expect `E_NAME_COLLISION_ALIAS_FIELD` at the bare `damage`; do not choose one interpretation or silently fall through from the alias to field shorthand. `damage.damage` would be the explicitly qualified spelling, though it is intentionally not required by this fixture.

### `event-field-for-wrong-event`

```c
behavior BadEventField {
  on Begin(GameProjectile projectile) {
    projectile.age += event.dt;
  }
}
```

Expect `E_EVENT_FIELD_NOT_FOUND` at `event.dt`, since Begin has no payload.

### `readonly-event-input`

```c
behavior BadWrite {
  on Tick(GameProjectile projectile) {
    event.dt = 0.0;
  }
}
```

Expect `E_ASSIGNMENT_READ_ONLY` at `event.dt`.

The same diagnostic is required for a reflected component field whose registry has no set-field operation:

```c
behavior BadReadonlyField {
  on Tick(ReadOnlyFixture sensor) {
    sensor.value = event.dt;
  }
}
```

### `duplicate-or-reserved-alias`

```c
behavior BadAliases {
  on Tick(GameProjectile projectile, GamePickup projectile) {
    destroy(self);
  }
}
```

Expect `E_COMPONENT_ALIAS_DUPLICATE`. Separate fixtures must reject `event` or `self` as component alias names with `E_NAME_RESERVED`.

Also reject two aliases of the same component type with `E_COMPONENT_CONTEXT_DUPLICATE`; an entity has at most one instance of a component type.

### `no-implicit-local`

```c
behavior BadLocal {
  on Tick(GameProjectile projectile) {
    elapsed += event.dt;
  }
}
```

Expect `E_NAME_UNRESOLVED` at `elapsed`. This slice does not create locals from assignment targets.

### `type-and-boolean-checks`

```c
behavior BadTypes {
  on ContactBegin(GameProjectile projectile) {
    projectile.speed = event.other;
    if (projectile.age) {
      destroy(self);
    }
  }
}
```

Expect a type error for assigning the entity handle `event.other` to the real field `projectile.speed`, and a separate non-boolean-condition error for `if (projectile.age)`. Diagnostics must point to the offending member/expression and leave graph and map unchanged.

## Identity safety cases

- Insert, remove, edit, and reorder unique statements without markers; unchanged statements retain their graph IDs.
- Keep repeated identical statements unchanged; their existing IDs remain stable.
- Insert, delete, or edit among repeated unmarked statements where more than one match is possible; expect `E_SOURCE_IDENTITY_AMBIGUOUS` before graph mutation.
- Add distinct `// @node` labels and compile the unchanged block once; then reorder or edit those statements and verify that the labels preserve their IDs.

## Typed collection cases

```c
behavior SeenItems {
  state int[] ids = [1, 2, 1];
  state set<int> seen = [];
  on Tick() {
    for (id in ids) {
      if (seen.contains(id)) {
        seen.remove(id);
      } else {
        seen.add(id);
      }
    }
  }
}
```

Expect state arrays and sets to persist across handler calls; `ForEach` to visit array values in source order; its element binding to be body-scoped and read-only; and set membership/add/remove to lower to `Set Contains`, `Set Add`, `Set Remove`, `Branch`, and `Set Variable` nodes. A set item with a different element type must report `E_SET_ELEMENT_TYPE`. Attempting set iteration or using a loop binding after its body must fail before graph publication.

The `ContactRegistry` example also checks `state set<entity> contacted = []` with a `ContactBegin` handler. `event.other` must resolve to the reflected `Other: ecs.entity` output and connect directly to the set node; non-empty entity defaults and unrelated event field types are rejected.
