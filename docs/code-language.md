# The Chibi language

The three code blocks marked `SPDX-License-Identifier: MIT` are copy-paste
examples under [the MIT license](../COPYING.EXAMPLES). Keep their copyright
and permission notice when redistributing them. Other examples retain their
[documented license](../LICENSE.md); this carveout does not cover the runtime,
core libraries, generated scaffolding, or third-party material.

Chibi uses `.chibi` files for functions and gameplay logic. Scenes and project
configuration use KDL. The `.chibi` source contains the whole executable asset.
Editors keep graph layout, watches and breakpoints in session.

This overview introduces the language through small examples. Game examples
assume that the project declares the components, prototypes and input actions
they use. See [Examples](examples.md) for complete recipes.

## Declarations

Use `:=` for a mutable value, `:` for an explicit type, and `::` for a constant.

```chibi
Example :: library {
    Run :: () {
        score: int = 0
        speed := 120.0
        GameTitle :: "My Game"
        MaxLives: int : 3

        score += 10
    }
}
```

A numeric literal takes its type from context. Otherwise, its spelling supplies
the type. Integer spelling gives `int`, then `i64` or `u64` when needed.
A decimal point or exponent gives a `number`, a 32-bit float. In an `f64`
context, a literal keeps all 64 bits. A literal past the `number` range needs
that context: `x: f64 = 1e300`, `f64(1e300)` and `pow(Base: -1e300, Exponent: x)`
with an `f64` `x` work, while `x := 1e300` and `sqrt(1e300)` fail.

```chibi
Example :: library {
    Run :: () {
        lives := 3        // int
        elapsed := 0.0    // number
        half := 1 / 2     // 0: integer division
        ratio := 1 / 2.0  // 0.5
        visible := true
        message := "Ready"
    }
}
```

Constants cannot change. Their initializers must be available at compilation.
Mutable values declared inside a function or handler belong to that invocation.

Use `elapsed := 0.0` for a timer. `elapsed := 0` creates an integer, so adding
a floating time step fails. Initialize the timer with `0.0`. Beside a typed operand,
a literal takes that operand's type. `count * 0.5` fails when `count` is an
integer. Use `number(count) * 0.5` for a floating result.

`//` starts a comment. A newline can end a statement. Semicolons separate
multiple statements on one line.

```chibi
Example :: library {
    Run :: () {
        score: int = 0
        // Increase the score once.
        score += 10
    }
}
```

## Values and operators

Common value types include `bool`, `int`, `number`, `string`, `entity` and arrays.
`number` is a 32-bit float on the CPU and the GPU, and `int` is a 32-bit signed
integer. Code that needs other widths names them with Odin's sized types:
`i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `f32` and `f64`. `i32`
is `int` and `f32` is `number`.

```chibi
Example :: library {
    Run :: () {
        health: int = 100
        damage: int = 25
        paused := false
        remaining := health - damage
        alive := remaining > 0
        canFire := alive && !paused
    }
}
```

Arithmetic uses `+`, `-`, `*`, `/` and `%`. Comparisons use `==`, `!=`, `<`,
`<=`, `>` and `>=`. Boolean expressions use `&&`, `||` and `!`.

An explicit conversion calls the destination type. Integers widen
implicitly, and so does `number` to `f64`. Narrowing `f64` to `number` requires
`number(value)` or `f32(value)`, and floating-to-integer conversion requires a
call such as `int(value)`. Integer narrowing clamps a value that does not fit to
the nearest bound. `wrap` keeps the low bits instead.

```chibi
Example :: library {
    Run :: () {
        value: int = 300
        limited := u8(value)        // 255
        wrapped := u8(wrap: value)  // 44
    }
}
```

No integer operation fails at run time. Arithmetic wraps on overflow. `x / 0`
is 0 and `x % 0` is `x`. Dividing the signed minimum by -1 wraps to the
minimum, and its remainder is 0. Floating-to-integer conversions truncate
toward zero, clamp at the bounds, and map NaN to zero. A development build
reports each overflow, zero divisor and clamping conversion once per source
site, and keeps these values.

Left shifts discard outgoing bits. Signed right shifts extend the sign bit;
unsigned right shifts insert zeros. A count at or past the operand's bit width
shifts every bit out: the result is 0, or -1 for a signed right shift of a
negative value. The count is read as unsigned, so a negative count is past the
width. The operation uses its resolved operand type, even when its result is
then converted to a wider type.

A character literal contains one Unicode scalar. A string contains text.
Runtime strings cannot contain embedded NUL bytes.

```chibi
Example :: library {
    Run :: () {
        separator: char = '/'
        letter := '🐈'
        label := "Player\nReady"
    }
}
```

## Functions

A function names its parameters and result type. Calls pass arguments in
parameter order.

```chibi
// chibi-mit-example: damage-functions
// SPDX-FileCopyrightText: 2026 Charles Waldner
// SPDX-License-Identifier: MIT
Example :: library {
    DamageFor :: (level: int) -> int {
        return level * 2
    }

    DamageAfterArmor :: (damage: int, armor: int) -> int {
        if damage <= armor { return 0 }
        return damage - armor
    }
}
```

Arguments can also name a parameter. Parameters can declare defaults.

```chibi
Example :: library {
    Bonus :: (points: int, multiplier: int = 2) -> int {
        return points * multiplier
    }

    Reward :: () -> int {
        return Bonus(10, multiplier: 3)
    }
}
```

A function without a result annotation infers its result from the first
`return`. Recursive functions must declare their result types.

```chibi
Example :: library {
    Double :: (points: int) { return points * 2 }
}
```

A function with results must return on every path. Multiple results use names
and return in signature order.

```chibi
Example :: library {
    SplitDamage :: (damage: int) -> (shield: int, health: int) {
        return(damage / 2, damage - damage / 2)
    }
}
```

Parameters normally receive value copies. A `^T` parameter receives a pointer
to the caller's value, so the callee can change it. Pass `&value` at the call
site.

```chibi
// chibi-mit-example: pointer-parameter
// SPDX-FileCopyrightText: 2026 Charles Waldner
// SPDX-License-Identifier: MIT
Example :: library {
    AddScore :: (score: ^int, points: int) {
        score^ += points
    }

    Run :: () -> int {
        score := 10
        AddScore(&score, 5)
        return score // 15
    }
}
```

When the callee only reads and writes through the pointer, reads its target's
fields, or passes it on to another such parameter, the compiler lends the
caller's location for the call instead of keeping it alive. Such a parameter
also accepts a component or state field, `AddScore(&self.Score.value, 5)`, and
an existing pointer, which lends its target. Any other use, such as storing or
returning the pointer, makes it an ordinary managed pointer.

`&value` must name a writable value of the parameter's exact type. A parameter
that only passes its pointer on writes the name alone: `AddScore(score, 5)`.

Copy avoidance and ownership transfers are compiler details. Calling a function
does not require a `move` keyword or invalidate an ordinary value.

The compiler infers effects and purity. Functions need no separate `pure`
keyword. Runtime calls have a shared depth limit of 64.

## Managed pointers

The managed-pointer implementation is available on the local implementation
branch. Its contract and check results are tracked in
[Managed pointers](managed-pointers.md).

`^T` points to a mutable value of type `T`. Take an address with `&value`,
access the target with `pointer^`, or access its fields directly. Copying a
pointer keeps referring to the same target. Copying a record copies its field
values, so pointer fields in the two records still refer to the same targets.

```chibi
Example :: library {
    Actor :: struct { health: int; }

    Run :: () -> int {
        actor: Actor = {health: 100}
        copy := actor
        pointer := &actor
        alias := pointer
        copy.health = 50
        alias.health = 80
        return actor.health // 80
    }
}
```

A pointer keeps its target alive after the declaring function returns.
Unreachable cycles are collected automatically. No ownership-transfer keyword
is needed. Memory reclamation does not replace explicitly closing a file or
another external resource.

`^T?` permits `nil`; check a nullable local before dereferencing it. `^T[]`
is an array of pointers, and `^(T[])` points to an array. A postfix caret must
touch its operand: `pointer^`. Binary xor has spaces: `a ^ b`.

Taking a managed address currently requires a writable local, value parameter
or field of a flat authored record. Managed payloads containing inline nested
records or native reflected records are rejected until their layouts can be
retained safely. Array and map elements do not yet support address-taking.
Managed pointers cannot cross ordinary native-service or persistence boundaries.

## Conditions and loops

`if` selects a branch. Conditions do not require parentheses.

```chibi
Example :: library {
    Message :: (health: int) -> string {
        message := ""
        if health <= 0 {
            message = "Game over"
        } else {
            message = "Continue"
        }
        return message
    }
}
```

`switch` evaluates its value once. Cases do not fall through. An empty `case:`
is the default.

```chibi
Example :: library {
    DamageFor :: (level: int) -> int {
        damage: int = 0
        switch level {
            case 1:
                damage = 10
            case 2:
                damage = 20
            case:
                damage = 30
        }
        return damage
    }
}
```

`while` repeats while its condition is true. `break` leaves a loop.
`continue` starts the next iteration.

```chibi
Example :: library {
    Run :: () {
        remaining: int = 5
        while remaining > 0 {
            remaining -= 1
            if remaining == 2 { continue }
            if remaining == 0 { break }
        }
    }
}
```

A range with `..<` excludes its upper bound. A range with `..` includes it.
`for <` visits a range in reverse order.

```chibi
Example :: library {
    Run :: () {
        score: int = 0
        for i in 0..<4 { score += i }    // 0, 1, 2, 3
        for i in 1..3 { score += i }     // 1, 2, 3
        for < i in 0..<4 { score += i }  // 3, 2, 1, 0
    }
}
```

Ranges do not reverse automatically. A descending constant range such as
`10..0` is an error.

## Arrays and maps

An array type ends in `[]`. Array literals use square brackets.
An empty array needs an explicit element type.

```chibi
Example :: library {
    Run :: () {
        scores: int[] = [10, 20, 30]
        empty: int[] = []
        first := scores[0]
        scores[1] = 25
        scores.append(40)
        count := scores.length()
    }
}
```

Array indices start at zero. An index outside the array fails the invocation.
Every array function is also a method on its first argument: `xs.length()`,
`xs.contains(x)`, `xs.slice(start, count)`. `xs.append(x)` adds an item and
`xs.extend(ys)` adds another array's items. Methods that change their array
(`append`, `extend`, `insert`, `remove_index`, `remove_item`, `add_unique`,
`set_array_elem`, `clear`, `reverse`, `swap` and `resize`) are statements on
the array's name and change it in place; using one in an expression is an
error. As functions, they return a changed copy instead: `ys := append(xs, x)`
and `zs := extend(xs, ys)` leave `xs` as it was. Project version 32 renamed
`array_add` to `append` and the concatenating `append` to `extend`.

Arrays have value semantics. Changing a copy leaves the original unchanged.
Appending to an owned local array and writing its elements update that local
storage in place.

```chibi
Example :: library {
    Run :: () {
        original: int[] = [1, 2]
        copy := original
        copy[0] = 9 // original[0] remains 1.
    }
}
```

Array iteration binds the item first and an optional index second.
Ordinary iteration takes a snapshot before the loop.

```chibi
// chibi-mit-example: weighted-total
// SPDX-FileCopyrightText: 2026 Charles Waldner
// SPDX-License-Identifier: MIT
Example :: library {
    WeightedTotal :: (scores: int[]) -> int {
        total: int = 0
        for value, index in scores {
            total += value * index
        }
        return total
    }
}
```

`for &value in scores` changes the items of a mutable owned array: `value`
names each item itself. The loop cannot take a key, access the source array
directly or suspend.

```chibi
Example :: library {
    Run :: () {
        scores: int[] = [10, 20, 30]
        for &value in scores {
            value += 10
        }
    }
}
```

A map `[K: V]` associates keys of one type with values of another, as Swift
writes it. `["Player": 10]` is a map literal and `[:]` an empty one; an empty
literal takes its types from the declaration. Reading a missing key yields the
value type's zero, as in Odin: `0`, `""`, an empty array or map, a record's
field defaults, or `nil` for an optional value. Reading adds no key, so
`counts[name] += 1` counts from zero. A record with a field that has no default
has no zero value, so reading a missing key of a map of them fails; check the
key first. Writing through a missing key, as `m[a][b] = v` when `m` lacks
`a`, also fails; store `m[a]` first.

`k in m`, `k not in m` and `m.has(k)` test for a key. `delete m[k]` and
`m.delete(k)` remove one, and removing a missing key does nothing.

```chibi
Example :: library {
    Run :: () {
        scores: [string: int] = ["Player": 10, "Rival": 20]
        scores["Player"] = 30
        scores["Guest"] += 5
        if "Rival" in scores {
            delete scores["Rival"]
        }
        empty: [string: int] = [:]
    }
}
```

Map iteration visits a snapshot in insertion order. Replacing a value keeps
the key's position. Map equality compares associations independently of order.

A lookup takes constant time: a map of more than a few entries keeps Odin's
Robin Hood hash index over its entries. Storing a key and removing one update
a local map in place, as appending updates a local array, so building a map
of n keys takes O(n). A removal shifts the later entries down to keep their
order, so it takes time in proportion to the map.

```chibi
Example :: library {
    Total :: (scores: [string: int]) -> int {
        total: int = 0
        for name, points in scores {
            total += points
        }
        return total
    }
}
```

## Text

String iteration yields one string per Unicode scalar. It does not yield bytes
or complete displayed characters that contain several scalars.

```chibi
Example :: library {
    Run :: () {
        for character in "Aé🐈" {
            // character is "A", then "é", then "🐈".
        }
    }
}
```

Packages can import `core:strings` for text operations.

```chibi
package names
import strings "core:strings"

pub IsImage :: (path: string) -> bool {
    return strings.HasSuffix(path, ".png")
}
```

`ByteLength`, `Bytes`, `Slice`, `Index`, `Split`, `Join`, `TrimSpace` and related
functions use byte offsets. Slices can split UTF-8 sequences. Case conversion
and whitespace checks use ASCII rules.

See [Core packages](packages.html) for function signatures and descriptions.

## Records and unions

A record gives names and types to its fields. A field without a default must
appear in the initializer.

```chibi
Example :: library {
    ScoreEntry :: struct {
        name: string
        points: int = 0
    }

    Run :: () {
        scoreEntry: ScoreEntry = {name: "Player"}
        scoreEntry.points = 10
    }
}
```

Authored record fields currently support scalar values. Arrays, unions and
nested records are not available as authored record fields.

A tagged union stores one alternative at a time. An alternative can have a
payload.

```chibi
Example :: library {
    AttackState :: union {
        Idle | Windup(remaining: number) | Active(damage: int)
    }

    Run :: () {
        ready := false
        countdown: number = 0.0
        health: int = 100
        phase := AttackState.Windup(0.3)
        switch phase {
            case .Idle:
                ready = true
            case .Windup(remaining):
                countdown = remaining
            case .Active(damage):
                health -= damage
        }
    }
}
```

A switch over a finite union must cover every alternative or include `case:`.
Union values need an initializer. ECS component fields cannot contain unions.

`is` tests and narrows a union value. `as T` extracts an optional `T`.
A failed extraction gives `nil`. A successful extraction preserves zero,
false and empty text as present values.

```chibi
Example :: library {
    Run :: () {
        total: number = 0.0
        result: number | Cancelled = 0
        if result is number amount {
            total += amount
        }

        amount := result as number
        if amount {
            total += amount
        }
    }
}
```

`a ?? b` reads an optional's value, or `b` when it is `nil`, as Swift's `??`
and Odin's `or_else` do. `b` runs only when `a` is `nil`. It has `a`'s value
type, or is optional itself, which keeps the result optional. `??` groups to
the right and binds below arithmetic and above comparison, so
`a ?? b ?? 0 + 1 < limit` reads `(a ?? (b ?? (0 + 1))) < limit`.

```chibi
Example :: library {
    Total :: (bonus: int?, scores: [string: int?]) -> int {
        return (bonus ?? 0) + (scores["Ada"] ?? scores["Bea"] ?? 0)
    }
}
```

## Packages and visibility

A package is a folder of `.chibi` files with the same `package` header.
Sibling files share functions, constants, records and unions.
Create `score/Bonus.chibi` in your package root:

```chibi
package score
// score/Bonus.chibi

Twice :: (points: int) -> int { return points * 2 }
pub Reward :: (points: int) -> int { return Twice(points) + 5 }
```

Declarations are private to the package unless `pub` exports them.
Other packages can call `Reward` but cannot call `Twice`.
Create `rewards/Main.chibi` beside the `score` folder:

```chibi
package rewards
// rewards/Main.chibi
import score "project:score"

pub StartingReward :: () -> int {
    return score.Reward(10)
}
```

Import aliases belong to their file. Sibling files do not inherit them.
`pub` can export functions, constants, records and unions. It follows any
attributes and precedes the declaration name.

Game projects currently use named library blocks for reusable functions.
Their names resolve through the project catalog.

```chibi
Scoring :: library {
    Reward :: (points: int) -> int { return points * 2 }
}
```

A game calls this function as `Scoring.Reward(10)`.
Library, behavior and system blocks do not take `pub`.

## Generics and compile-time values

`$T` binds a type from an argument. Later uses of `T` refer to that type.
`$N: int` binds an integer that must be known at compilation.

```chibi
Example :: library {
    Identity :: (value: $T) -> T { return value }
    Scale :: (value: $T, $N: int) -> T { return value * N }
}
```

Ordinary functions can calculate constants when their inputs and operations
are available at compilation.

```chibi
Example :: library {
    Factorial :: (n: int) -> int {
        if n <= 1 { return 1 }
        return n * Factorial(n - 1)
    }

    Size :: Factorial(5)
}
```

Compile-time execution supports scalar and type values, authored calls, locals,
assignment, `if` and `while`. It does not support arrays, `for`, closures or
native calls. Each evaluation has a 65,536-step budget and a 128-call limit.

Generic package functions currently specialize only within their package.
Qualified game-library calls can specialize across library boundaries.

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

Live replacement preserves matching retained values. New or incompatible
bindings use their defaults. Replacement cancels old continuations and tasks.
An explicit restart resets the owner's retained state.

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

A system runs its body for each matching entity. Its header names the required
components. `?` makes a component optional. `without` excludes components.

```chibi
EmitterControl :: system(
    emitter: ParticleEmitter,
    trigger: Aim?
) without Collider on Tick :: (dt: s) {
    if trigger {
        emitter.Active = trigger.Firing
    }
    emitter.Rate += dt
}
```

The optional `on Tick` clause names the step fields that the system reads.
Only the system body can read them. Pass a step value to helpers that need it.
Systems cannot read `event`. Omit the clause when the body needs no step fields.

Optional component fields need a presence guard. An `await` invalidates the
proof, so check presence again after suspension.

System-body locals belong to each query invocation. They survive suspension
until that invocation completes. Entity state belongs in components when it
must outlive the invocation.

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

## Waiting and tasks

`await` suspends the current continuation until an operation completes.
Other engine work continues. An awaited call can declare its result.

```chibi
Spawner :: behavior {
    on Begin :: () {
        await delay(1)
        created := await spawn_prototype(Emitter)
        created.ParticleEmitter.Parent = self
    }
}
```

Calling a waiting function without `await` starts a task. The task runs until
its first suspension, then continues alongside its caller.

```chibi
Charger :: behavior {
    Charge :: (seconds: number) -> number {
        await delay(seconds)
        return seconds * 2
    }

    on Begin :: () {
        charging := Charge(1)
        cancel charging after 2
        result := await charging
        if result is not Cancelled {
            self.Health.current += int(result)
        }
    }
}
```

A `task(T)` binding holds a task that finishes with `T`, and awaiting it gives
`T | Cancelled`. Cancellation never becomes a default
successful value. `if taskHandle` tests whether a task is running.

`yield` resumes on the next update. `cancel taskHandle` stops the task and its
children. `cancel taskHandle after seconds` sets a deadline relative to the
task's original start time.

Task handles belong to the entity or system registration that started them.
Removing that owner stops its tasks. Dropping the last handle does not cancel
a running task.

## Units

Numeric parameters and results can declare dimensions. The compiler checks
known dimensions at calls, assignments and returns.

```chibi
Example :: library {
    Distance :: (speed: u/s, elapsed: s) -> u {
        return speed * elapsed
    }

    ChargeRate :: (charge: number charge, elapsed: s) -> number charge/s {
        return charge / elapsed
    }
}
```

Known unit names use `number` storage. An explicit type can precede the unit,
such as `f64 u/s`. Custom single dimensions need an explicit numeric type.
Units do not change runtime storage.

## Tests and formatting

`@test` marks a package-level function with no parameters and no results.
`assert` fails the test when its condition is false.

```chibi
package scoring

Double :: (points: int) -> int { return points * 2 }

@test
DoublesPoints :: () {
    assert(Double(3) == 6, "three doubles to six")
}
```

Run tests and format source with the command line tools.

```sh
chibi test path/to/packages
chibi fmt path/to/project
chibi check path/to/project
```

Independent tests run in parallel by default, with private execution state and
source-ordered results. Tests with external effects run alone. Set
`CHIBI_TEST_WORKERS=1` for serial debugging. Tests run without a game and cannot
wait or start tasks.
See [Testing](guide/testing.md) and [Toolchain](guide/toolchain.md) for commands
and platform limits.

## Current limits

Some language features remain unavailable:

- Anonymous function values and closures.
- Array, union and nested-record fields in authored records.
- Union fields in ECS components.
- Exported generic specialization between packages.
- Embedded NUL bytes and multiline string literals.

These examples describe the language accepted by the native frontend. The
authored stage-one package compiler supports a smaller subset. It still needs
explicit local types and does not compile reference iteration. It supports a
restricted set of maps, integer switches, range loops, indexed loops, reverse
loops and literal parameter defaults. Package imports enforce `pub` visibility.
See [the package compiler](package-compiler.md) for its supported subset and
[language friction](language-friction.md) for remaining stage-one gaps.

See the generated [Grammar](grammar.html) for syntax and
[Examples](examples.md) for gameplay recipes.
