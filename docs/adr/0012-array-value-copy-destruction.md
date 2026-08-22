# ADR 0012: Array value ownership and per-type operations

Status: accepted

## Decision

Array values carry the element pin type and use one ownership protocol in every
path. `pin_array` owns its item storage. Heap-owned runtime arrays are copied
with `pin_value_copy` and released with `pin_value_destroy`; activation arrays
are cloned into the activation arena and are never individually freed. Graph
defaults are copied into graph-owned blocks.

Scalar strings use owned copies. Struct and handle values remain borrowed from
their existing descriptor or host contract until a type-specific copy callback
is registered. Equality dispatch compares scalar values, names by spelling,
handles by nominal type and opaque value, and arrays element by element.

The callback boundary is deliberately narrow. Adding a new nominal element
kind must provide copy, destruction, and equality behavior before the kind can
be used as an array element. Array operations do not infer ownership from C
pointer spelling.

## Consequences

- Runtime instances can copy and destroy array defaults without mutating the
  authored Blueprint.
- Array operations can use arena-owned results for one activation.
- Save/load writes authored array elements only; runtime arrays and activation
  storage remain absent from JSON.
- Set and Map hashing remain deferred until a separate key-trait decision is
  accepted.
