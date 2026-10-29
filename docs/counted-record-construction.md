# Local array construction

Building an array in a loop is linear. A local or result array owns its items,
because every store into it copies. So lowering turns `xs = append(xs, x)`,
`xs = extend(xs, ys)` and `xs[i] = x` on that local into the `array_push`,
`array_extend` and `array_store` intrinsics, which update the array where it
is. Capacity doubles as in Odin's
dynamic arrays, so each append costs amortized O(1) and the old buffer is
reclaimed by the next collection. An out-of-range write fails with
"Array index out of range".

Values that hold the array keep their copy: another local, a record field, a
union payload, a `foreach` snapshot and a borrowed argument all see the array as
it was. Parameters alias the caller's array, so assigning to one keeps the
copying path. Other collection natives (`set_array_elem`, `insert`) still
return a new array.

The statement keeps the call's debugger points and watches; its assignment
point follows the value. A query predicate may update its own local arrays.
Effect analysis does not follow a local across a branch, so a predicate that
reads the array after an update whose value branches (`a && b`) is rejected, as
a read after any other branch already was.

Collectable arenas index their live values by page, so each record copy's
ownership check does not walk the arena.

`language-row-array` covers the current behavior: 200,000 appends, 100,000
record appends, aliasing through copies, borrowed arguments, `foreach`, record
items and union payloads, on bytecode and the compiled backend, in ordinary and
collectable storage. `language-program` collects a suspended handler's payload
mid-build and checks that only the current buffer survives, and
`record-c-export` runs the three updates through generated C.
