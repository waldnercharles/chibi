# Behavior Expression

Find **Behavior Expression** in Add-node search. Select its call and edit **Source**
in Inspector, then press Enter. For example, `(1 + x) * sin(myVar) - 2.4 / rand()` infers
inputs named `x` and `myVar`. Reusing an input name preserves its pin identity,
default, and compatible wires. Renaming an input in place preserves that input's
pin identity, wire, default, and binding, and updates every call to the expression.
When several names change together, the editor pairs unmatched old and new names
in pin order; make one rename at a time when an edit also adds or removes inputs.
An invalid edit leaves the working graph unchanged.

Expressions accept decimal numbers, identifiers, parentheses, unary `+`, `-`, and
`!`, and `+`, `-`, `*`, `/`, `%`, `^`, and `**`. Comparisons use `==`, `!=`, `<`,
`<=`, `>`, and `>=`; boolean expressions use `&&` and `||`. Both power operators
associate right to left. Multiplication and division bind before addition and
subtraction.
The constants `pi` and `e` are built in. Use parentheses to make grouping explicit.

| Functions | Arguments |
| --- | --- |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan` | One angle or scalar; angles use radians |
| `sqrt`, `exp`, `log`, `ln`, `log10` | One scalar; `log` and `ln` are natural logarithms |
| `floor`, `ceil`, `round`, `fract`, `sign`, `abs`, `negate` | One scalar |
| `min`, `max`, `pow` | Two scalars |
| `mix` | `a, b, alpha`; `a` at 0, `b` at 1 |
| `step` | `edge, x`; 0 below the edge, 1 from it on |
| `length`, `normalize` | One `CF_V2` |
| `dot`, `distance` | Two `CF_V2` |
| `atan2` | `y, x` |
| `clamp` | `value, minimum, maximum` |
| `rand` | No arguments; returns a value from 0 inclusive to 1 exclusive |
| `vec2` | `x, y`; constructs a `CF_V2` |
| `vec3` | `x, y, z`; constructs a `CF_V3` |

## Shader and field graphs

The same language works in materials, shapes, shader libraries and procedural
fields. One table in `graph/nodes/math_expression.c` names the behavior, shader
and field node behind each function; a function a graph kind lacks reports
`floor() is not available in shader graphs`. Shader and field expressions are
values only: no assignments, comparisons or boolean logic, and `%` and `^` need
a behavior graph.

| Graph | Functions |
| --- | --- |
| Shader | `sin`, `cos`, `fract`, `abs`, `min`, `max`, `clamp`, `saturate`, `mix`, `step`, `smoothstep`, `length`, `dot`, `normalize`, `noise(position, scale)`, `vec2`, `vec3`, `vec4` |
| Field | `sin`, `cos`, `floor`, `abs`, `negate`, `min`, `max`, `clamp`, `remap`, `select`, `noise(x, y, seed)`, `hash(x, y, seed)`, `fractal`, `circle`, `box`, `capsule`, `rotate_x`, `rotate_y`, `smooth_union`, `smooth_subtract` |

Arguments follow GLSL: `step(edge, x)` and `smoothstep(edge0, edge1, x)`. In
shaders an input keeps the width of the value wired into it, operations take
their widest operand, and a scalar operand broadcasts: `color * glow` scales a
`ShaderVec3`. Mixing 2- and 3-component operands is an error. Members swizzle:
`uv.y`, `color.zyx`, `color.rgb`. A member read passes the input through until
the input is connected, then regenerates with the real width. Field
expressions read and return one float.

**Convert to Expression** works on shader and field math too: it spells each
node with the table above, in GLSL argument order, with spaces and only the
parentheses precedence needs. Converted inputs are named after their source:
a parameter's name, a shader or field built-in (`X`, `UV`), or a component
field as `transform_x`.

Inputs and the result infer their connected graph types. Numeric operators support
scalar broadcasting across reflected numeric structs such as `CF_V2` and
`GameTransform`; the result keeps the aggregate type. Use member access such as
`pos.x + pos.y` to project scalar fields from a connected struct input. The input
must be connected before a field expression can regenerate successfully. Integer-to-Real and explicit Real-to-integer conversions are
available. A Real result can feed Byte, Integer, or Integer64 through a conversion edge. Finite
fractional values truncate toward zero; values outside the destination range,
including infinities, clamp to its bounds; NaN becomes zero. Float and double
boundaries still follow the graph's pin types.

Behavior Expression also works in System graphs, including creation from a dragged
scalar or structured output pin. The connected input belongs to the inferred
expression signature. Press Enter to regenerate from the expression Inspector.

Applying Source parses the expression into ordinary graph nodes in a generated
function; runtime execution uses that graph rather than evaluating arbitrary text.
An assignment's leftmost identifier names its binding input. Choose a name that
matches the value being written, such as `position = value`, `position += amount`,
`aim.direction.x = desired_x`, or `items[index] *= scale`. The leftmost input uses
the existing binding picker. Assignments expose `Exec`, `Then`, and a `Result`
containing the value after the write. The binding is stored by variable or
component identity, so renaming a variable or changing an expression input does
not turn it into a name lookup.
Boolean expressions retain their boolean `Result`. When placed in an executable
behavior graph they also expose `Exec`, `True`, and `False`; dataflow and pure
contexts keep the boolean result without flow pins.
**Inspect generated graph** opens the ordinary function graph used to evaluate
the expression. Its contents are read-only; edit the source to regenerate them.
Deleting that generated definition removes its calls too. Undo restores the
definition and calls together; source edits also support Undo/Redo.

To replace an existing calculation, select its connected pure numeric math nodes,
open the node context menu, and choose **Convert to Behavior Expression**. The editor
preserves incoming wires and component or variable bindings, including reflected
numeric structs such as `GameTransform`, reconnects the new call to the selected
result's consumers, and selects the replacement call. A selection may also end at
one terminal result before it has a consumer. Keep state, component reads,
execution nodes, and time reads at the boundary of the selection.

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
