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

A character literal contains one Unicode scalar. A string is an immutable byte
sequence with a stored length. Strings may contain embedded NUL bytes or invalid
UTF-8; Unicode validation and decoding are explicit operations.

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

Mark a package-owned struct or union with `@error` to designate an error type.
A function returning that type, an optional of it, or a transparent union
containing it requires callers to acknowledge its final result. Earlier results
remain usable, including partial progress returned with an error. Ordinary
optionals do not imply failure. See [recoverable errors](recoverable-errors.md)
for checking, union composition and compiler-subset limits.

Parameters follow each type's assignment rule: scalars, records, arrays and
maps copy their values, and managed pointers keep their targets. A `^T`
parameter receives a pointer to the caller's value, so the callee can change
it. Pass `&value` at the call site.

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

A pointer to an ordinary language value keeps its target alive after the
declaring function returns.
Unreachable cycles are collected automatically. No ownership-transfer keyword
is needed. Memory reclamation does not replace explicitly closing a file or
another external resource.

`^T?` permits `nil`; check a nullable local before dereferencing it. `^T[]`
is an array of pointers, and `^(T[])` points to an array. A postfix caret must
touch its operand: `pointer^`. Binary xor has spaces: `a ^ b`.

Taking a managed address currently requires a writable local, value parameter
or field of a flat authored record. Managed payloads containing inline nested
records or native reflected records are rejected until their layouts can be
retained safely. Query bindings such as `health: ^Health` provide managed
references to reflected components, including native components with nested
records. The reference retains its identity and exact schema; removing the
component or destroying its entity or world permanently invalidates its
target. A later add creates a separate target. Invalid-target reads and writes
stop the activation with a runtime diagnostic. Authored code does not yet have
a catch or checked recovery operation. Fields reached through an existing
managed pointer can be projected without retaining an ECS row address. Array
and map elements do not yet support address-taking.
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
a writable array receiver and change that value. Mutation returns a bool indicating success. As functions, they return a changed copy instead: `ys := append(xs, x)`
and `zs := extend(xs, ys)` leave `xs` as it was. Project version 32 renamed
`array_add` to `append` and the concatenating `append` to `extend`.

Dynamic arrays and maps have value semantics. Assignment, ordinary arguments
and returns produce independent values. Copies may share backing storage until
one is changed. Nested arrays and maps follow the same rule; pointer elements
keep their shared targets. `clone()` copies the outer array.

```chibi
Example :: library {
    Run :: () {
        original: int[] = [1, 2, 3]
        copy := original
        copy[0] = 9 // original[0] remains 1.
        cloned := original.clone()
        cloned[0] = 7 // original[0] remains 1.
    }
}
```

`a[start..<end]` and `a.slice(start..<end)` copy a half-open range into a new
array. To share mutable positions explicitly, use a pointer: `p := &a`, then
`p.view(start..<end)`. A view retains that pointer and a fixed offset and length.
Both range forms return an optional: invalid bounds yield `nil`; a valid empty range is
present. Bounds must satisfy `0 <= start <= end <= a.length()`.

```chibi
Example :: library {
    Run :: () {
        values: int[] = [10, 20, 30]
        selected := values[1..<3]
        if selected {
            selected[0] = 90 // values[1] remains 20.
        }
        owner := &values
        window := owner.view(1..<3)
        if window {
            window[0] = 80 // values[1] is now 80.
        }
    }
}
```

A `view T[]` parameter or result names a view explicitly. Views support indexing,
iteration, element assignment, `reverse()`, `swap()` and `clone()`. Structural
operations such as append, insertion and removal belong to the owning array.
A view resolves the current array through its pointer on each access. Buffer
growth and reassignment through that pointer preserve the view. Its numeric
offset and length stay fixed; insertion or removal can change the elements at
those positions. Mutating the view changes the pointed-to array value and
preserves independent copies of that value.
If its whole range no longer fits after a shrink, use fails with a bounds error;
sufficient regrowth makes the view usable again.

The existing `slice(a, start, count)` and `a.slice(start, count)` operations
remain clamped copies. They clamp the start to the array bounds, treat a negative
count as zero, and limit the count to the remaining elements.

Array iteration binds the item first and an optional index second.
Ordinary iteration takes an independent array value before the loop. Changing
the source does not change that snapshot. Pointer elements keep their targets.

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

String indexing reads a byte. String range copies, `slice(range)`, `view(range)`
and `clone()` use the same bounds and copy/view distinction as arrays. String
views retain immutable bytes and do not permit writes. Offsets count bytes, so a
range may split a UTF-8 encoding. `strings.Slice(text, start, count)` keeps its
clamped start/count behavior.

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

Authored record fields can hold scalars, managed pointers, other records and
arrays of scalars, non-null pointers or records. Initialize aggregate fields
explicitly, including empty arrays. Aggregate field defaults, maps, unions and
arrays whose elements are themselves containers remain deferred.

Assignment copies a record's values. Mutating a copied record's nested fields or
array elements leaves the original unchanged. Pointer fields keep their target
identity when copied, so both records observe writes through the same pointer.
Replacing one record's pointer field affects only that record.

Records may refer to later record declarations. Recursive inline records and
recursive container fields are rejected; recursive links through pointer fields
remain supported. Aggregate records currently execute in the VM backends;
unsupported LLVM native lowering reports its rejection explicitly.

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
Union values need an initializer. A record field can hold a union; a record
literal must give it a value. ECS component fields cannot contain unions.

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

A postfix `!` takes an optional's payload and fails when it is `nil`, as Swift's
`!` does: `scores["Ada"]!` is the value, or a failed run with the message
"Unwrapped a nil optional". It must touch its operand, so `a!` unwraps and `a !b`
does not. Use it where absence is a bug; `??`, `if value` and `as T` handle
absence.

`value?.field` reads a field of an optional record: `nil` when the record is
`nil`, otherwise the field as an optional, so `(p?.x ?? 0) + 1` has a default.
`value!.field` unwraps first and fails on `nil`. Like `!`, `?.` must touch its
operand. It reads authored record fields only: not a call, not a field that
holds a record, and not an assignment target.

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

## Foreign libraries

Native builds call C libraries that were built beforehand, the way Odin's
`foreign import` does. A project never compiles the library's C source.

```chibi
foreign import libc "system:c"

foreign libc {
    getpid :: () -> i32
    abs :: (value: i32) -> i32
}
```

`foreign import alias "path"` names the library. `system:c` is libc, which a
native build links dynamically by default (`chibi build --native --static`
links none); another `system:` path is not linked yet. Any other path is a
prebuilt archive, relative to the importing package, that the native build adds
to the link line and records by hash in its receipt. `foreign alias { ... }`
declares the library's functions by signature alone. A function's name is its
C symbol.

Parameters and results are integers, floats, `bool` and `rawptr`, an opaque
64-bit word. A parameter may also be `cstring`, which takes any string. The call
copies it with a NUL into a 4096-byte stack buffer and passes the buffer, so the
cost is one small copy, visible in the declaration, with no heap allocation. A
string of 4096 bytes or more, or one that holds a NUL byte, traps instead of
being truncated. A result cannot be a `cstring`.

An array of numbers, such as `u8[]`, is passed as the pointer to its elements,
which the callee may write. The length is a parameter of its own, as in C, so
`read(fd, buffer, u64(length(buffer)))` fills `buffer` in place. The array is
lent for the call: nothing copies it, and C must not keep the pointer.

A foreign function is a native whose identity names the library path and the
symbol, so a target that does not link the library cannot load a program that
calls it; the VM never runs foreign code. Names are global like other natives,
so two foreign functions cannot share one.

### Function values from C

A C function's address has a function type, spelled like the callback types in
[passing functions](#passing-functions): `fn(n: i32) -> i32`. In a callback
parameter of an authored function it names a function the compiler knows, and
the algorithm is specialized for it. A function type that types a local, or a
foreign function's parameter or result, holds the address itself: a 64-bit
word, and a call through it is a C-ABI call.

```chibi
foreign import libc "system:c"

foreign libc {
    dlsym :: (handle: rawptr, name: cstring) -> rawptr
}

Main :: () -> int {
    absolute: fn(n: i32) -> i32 = dlsym(0, "abs")
    return absolute(-42)
}
```

A call through a null address traps. A foreign function may return a function type directly,
`dlsym :: (handle: rawptr, name: cstring) -> fn(n: i32) -> i32`, so
`absolute := dlsym(0, "abs")` takes its type from the result. A `rawptr` converts
to a function type without a cast, which is how one `dlsym` declaration serves
every signature. The parameters and result follow the foreign rules above. A
runtime address cannot yet be passed to an authored function's `fn` parameter;
call it where the local is. Passing an authored function by name where a foreign function takes an `fn`
parameter gives C an adapter the compiler generates for it, at no cost to the
call: `thrd_create(thread, Worker, 41)`. The function's signature must match the
parameter's, in the foreign types above. The adapter runs the function with the
program's single runtime context, so a function that C calls on another thread
must not allocate, assert or fail while another thread can; a C callback is
plain data in, plain data out.

Planned after that: `@variadic` with the number of fixed arguments for
variadic C functions, foreign functions without a result, and data symbols.

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

Compile-time execution supports integer and bool values, arrays of them,
records of them, authored calls, locals, assignment, `if`, `while`, `for`,
`break`, `continue` and `return`. It does not support floats, strings,
closures or native calls.
Each evaluation has a 65,536-step budget and a 128-call limit.
A constant initializer may call such a function, `Size :: Factorial(5)`, and
may combine calls with arithmetic and conversions; a `when` condition may not
call functions yet.

Arrays are local values: an array literal, `values: int[] = []`, `append`,
`length`, `values[i]` to read, and `values[i] = x` or `values[i] += x` to
change an element. Assigning or passing an array copies it. A function may
take and return arrays, and an index outside the array is a compilation error.
`for i in 0..<n`, `for i in 0..n`, `for < i in ...` (reverse), `for value in
array` and `for index, value in array` loop over a range or an array; loops
iterate by value, so a `for` over a `ref` is rejected.

A record whose fields are all integers and bools is a value too: `p: Point =
{x: 1, y: 2}` (each field once, in any order), `p.x` to read, `p.x = v` or
`p.x += v` to change a field, and a record may be passed to and returned from
a function. A record with an array, string or record field is rejected where a
function takes or returns it.

```chibi
Example :: library {
    Squares :: (count: int) -> int[] {
        values: int[] = []
        for i in 0..<count { values.append(i * i) }
        return values
    }

    Sum :: (values: int[]) -> int {
        total := 0
        for value in values { total += value }
        return total
    }

    Total :: Sum(Squares(5))  // 30
    Third :: Squares(6)[3]    // 9
}
```

```chibi
Example :: library {
    Point :: struct { x: int; y: int; }

    Shifted :: (p: Point, dx: int) -> Point {
        result := p
        result.x += dx
        return result
    }

    Right :: Shifted({x: 1, y: 2}, 10).x  // 11
}
```

### Choosing declarations per platform

`when` at package scope keeps the declarations of the branch whose constant
condition holds, so one source can pick a value or a function per target. The
condition reads literals and the constants of imported packages; `core:target`
supplies `Os`, `Arch` and `PointerBits` beside `Linux`, `Darwin`, `Windows`,
`X86_64` and `Arm64`. Conditions combine `==`, `!=`, `<`, `<=`, `>`, `>=`,
`and`, `or` and `not`. A branch that is not kept is parsed but never
compiled.

```chibi
package files
import target "core:target"

when target.Os == target.Darwin {
    StatSizeOffset: int : 96
} else when target.Os == target.Windows {
    StatSizeOffset: int : 72
} else {
    StatSizeOffset: int : 48
}
```

### Generic records

A record declares type parameters after `struct`. `$T: Type` takes a type and
`$N: int` takes an integer written in the application. Each distinct
application, such as `Box(int, 4)`, is its own record whose fields read with
the parameters bound to the arguments:

```chibi
package boxes

Box :: struct($T: Type, $N: int) {
    item: T
}

Pair :: struct($A: Type, $B: Type) {
    first: A
    second: B
}

Boxed :: struct {
    ints: Box(int, 4)
    nested: Box(Pair(int, f64), 2)
}
```

Equal applications are one type; applications with different arguments are
different types. The template alone, `Box`, names no type. A template and its
applications belong to one package for now: exporting generic records,
generic unions, and parameters used inside fields beyond type names remain
separate work.

### Exported package functions

The staged package-generics and callable work is a local implementation under
validation. The examples in these two subsections describe that contract;
they do not establish a merged SDK release or compiler-stage parity.
A matching reviewed bootstrap artifact and installed SDK checks remain pending.

The self-hosted route currently admits these forms in package sources and its
numeric scalar-facts subset. Its separate legacy behavior/library compiler does
not yet use package specialization or accept package imports. Stage-zero legacy
behavior support is a separate path. Exported generic record/type factories and
general compile-time execution parity remain separate work.

A package exports a generic function with the same `pub` marker as an ordinary
function. Its type bindings come from the importing call:

```chibi
package identity

pub Identity :: (value: $T) -> T { return value }
```

```chibi
package app
import identity "../identity"

Value :: () -> int { return identity.Identity(7) }
```

A specialization resolves private helpers, types and constants where its
function was defined. Caller declarations and import aliases do not change
that meaning. Private helpers stay private. Calls with equivalent concrete
bindings reuse a specialization; different nominal types keep their identities.
Changes to a defining package or its dependencies invalidate the corresponding
cached result. A failed specialization reports the importing call and its
relevant definition.

Qualified game-library calls can also specialize across library boundaries.

### Passing functions

A function parameter can describe another function with `fn(parameters) ->
Result`. Pass a function's name to supply its code; add parentheses to call it:

```chibi
package transforms

Double :: (value: int) -> int { return value * 2 }

Apply :: (value: int, transform: fn(value: int) -> int) -> int {
    return transform(value)
}

Example :: () -> int {
    return Apply(4, Double) // 8
}
```

An inline function can appear directly as a call argument. Inputs have explicit
types. A single expression after `=>` returns that expression; a block uses
ordinary `return` statements. Results can be inferred, or written with `->`.
The optional `fn` prefix has the same meaning:

```chibi
Inline :: () -> int {
    a := Apply(4, (value: int) => value * 2)
    b := Apply(4, (value: int) { return value * 2 })
    c := Apply(4, fn(value: int) -> int { return value * 2 })
    return a + b + c
}
```

The initial callable signature supports zero or one result. `fn(value: int)`
returns nothing; `fn(value: int) -> int` returns one integer. The callable
parameter takes no default value or `from`/`split` source binding. Its function
type may be parenthesized, but cannot be optional, placed in a container, or
wrapped in a pointer type.
Inputs in a callable signature have explicit types and no defaults or source
bindings. Calls supply every input; defaults on the supplied named function do
not become part of the callable signature.

A generic algorithm can use its element type in a predicate or comparator
signature, such as `predicate: fn(value: T) -> bool` or
`less: fn(a: T, b: T) -> bool`. The compiler specializes the algorithm for the
concrete function and emits direct calls. This preserves the callback's effects;
passing it as an argument does not make it pure. Inlining and the algorithm's
own allocations or value copies remain separate implementation questions.

These arguments finish synchronously. A function may call them or pass them to
another callable parameter, but cannot return, store or queue them, keep them
in records or containers, or give them to an unknown host API. An inline
function cannot capture enclosing runtime locals. Inline function syntax is
currently limited to call-argument positions, and callable types are limited to
function parameters. These rules introduce no runtime closure objects.

Captured and escaping functions remain separate work. Their later admission
must preserve the selected shared mutable lexical-binding semantics; this
capture-free stage does not change that decision.

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

A task also depends on each component its pointer arguments refer to when the
task may dereference that argument, directly or through a function it calls
and waits for. The dependency lasts until the task finishes, even after its
last access. Removing the component cancels the task and its children at the
next safe point, before its next continuation runs, even if the remaining code
would have skipped the access. Effects that already ran stay. A later add is a
different component and does not revive the task.

```chibi
Effects :: behavior {
    Poison :: (health: ^Health) {
        health.current -= 1
        await delay(2)
        play_sound(Sound: "poisoned") // Cancelled if Health was removed.
    }
}
```

Holding, comparing or returning a pointer does not add a dependency, and
neither does passing it to another task: that task takes its own. A child
does not take its parent's dependencies. Cancelling a running parent cancels
its children, but a parent that finishes leaves them running, each depending
only on its own components. A pointer the task reads back from state, a record
or another pointer is not tracked yet.

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

A test that must end with a run-time failure says so with `core:testing`,
as Odin's `expect_assert` does. `testing.ExpectFailure(kind)` makes the
running test pass only if it then ends with that kind of failure:
`IndexOutOfRange`, `MapKeyAbsent`, `ViewOutOfRange`, `AssertionFailed`,
`BudgetExhausted`, `DepthExceeded` or `UnwrappedNil`. Returning normally, or failing with
another kind, fails the test, and a failure before the call is an ordinary
failure. Nothing unwinds: the failure ends the test as it otherwise would.

```chibi
import testing "core:testing"

@test
PickPastTheEnd :: () {
    values: int[] = [1, 2]
    testing.ExpectFailure(testing.Failure.IndexOutOfRange)
    assert(values[2] == 3)
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

- Captured or stored function values and escaping callbacks.
- General inline-function expressions outside call arguments.
- Array, union and nested-record fields in authored records.
- Union fields in ECS components.
- Multiline string literals.

These examples describe the language accepted by the native frontend. The
authored stage-one package compiler supports a smaller subset. It still needs
explicit local types and does not compile reference iteration. It supports a
restricted set of maps, integer switches, range loops, indexed loops, reverse
loops and literal parameter defaults. Package imports enforce `pub` visibility.
See [the package compiler](package-compiler.md) for its supported subset and
[language friction](language-friction.md) for remaining stage-one gaps.

See the generated [Grammar](grammar.html) for syntax and
[Examples](examples.md) for gameplay recipes.
