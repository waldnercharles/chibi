# MathExpression

Find **Math Expression** in Add-node search. Select its call and edit **Source**
in Inspector, then press Enter. For example, `(1 + x) * sin(myVar) - 2.4 / rand()` infers
inputs named `x` and `myVar`. Reusing an input name preserves its pin identity,
default, and compatible wires. An invalid edit leaves the working graph unchanged.

Expressions accept decimal numbers, identifiers, parentheses, unary `+` and `-`,
and `+`, `-`, `*`, `/`, `%`, `^`, and `**`. Both power operators associate
right to left. Multiplication and division bind before addition and subtraction.
The constants `pi` and `e` are built in. Use parentheses to make grouping explicit.

| Functions | Arguments |
| --- | --- |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan` | One angle or scalar; angles use radians |
| `sqrt`, `exp`, `log`, `ln`, `log10` | One scalar; `log` and `ln` are natural logarithms |
| `floor`, `ceil`, `round`, `abs`, `negate` | One scalar |
| `min`, `max`, `pow` | Two scalars |
| `atan2` | `y, x` |
| `clamp` | `value, minimum, maximum` |
| `rand` | No arguments; returns a value from 0 inclusive to 1 exclusive |

Inputs and the result infer their connected graph types. Numeric operators support
scalar broadcasting across reflected numeric structs such as `CF_V2` and
`GameTransform`; the result keeps the aggregate type. Use member access such as
`pos.x + pos.y` to project scalar fields from a connected struct input. The input
must be connected before a field expression can regenerate successfully. Integer-to-Real and explicit Real-to-integer conversions are
available. A Real result can feed Byte, Integer, or Integer64 through a conversion edge. Finite
fractional values truncate toward zero; values outside the destination range,
including infinities, clamp to its bounds; NaN becomes zero. Float and double
boundaries still follow the graph's pin types.

MathExpression also works in System graphs, including creation from a dragged
scalar or structured output pin. The connected input belongs to the inferred
expression signature. Press Enter to regenerate from the expression Inspector.

Applying Source parses the expression into ordinary graph nodes in a generated
function; runtime execution uses that graph rather than evaluating arbitrary text.
**Inspect generated graph** opens the ordinary function graph used to evaluate
the expression. Its contents are read-only; edit the source to regenerate them.
Deleting that generated definition removes its calls too. Undo restores the
definition and calls together; source edits also support Undo/Redo.

Three supplied behaviors use expressions:

- `ProjectileBehavior`: `abs(x) + abs(y)` replaces the two absolute-value nodes
  and their sum in the direction check. Its consumer remains a float input.
- `MovementTrailBehavior`: `clamp(dt, 0, 0.10000000149011612) * 64` combines the
  bounded frame contribution and particle rate. The upper bound spells out the
  original float value of `0.1`, preserving the existing calculation's boundary.
- `HealthBehavior`: `clamp(current - clamp(amount, 0, current), 0, maximum)`
  combines damage bounds, subtraction, and health bounds. Its result uses
  To Integer at the existing health setter.

The sample conversions are checked at float boundaries, including subnormal
values and adjacent values around the trail clamp limit. Real-to-integer edges
also have graph evaluation and serialization checks for truncation, saturation,
and non-finite inputs. Health tests dispatch actual Damage events to check
integer limits, clamping, and destruction at zero health.
