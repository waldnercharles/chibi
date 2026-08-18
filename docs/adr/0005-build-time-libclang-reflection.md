# ADR 0005: Reflection is a build-time libclang tool that emits generated C

Status: Accepted
Date: 2026-08-18

## Context

Node types are declared by annotating ordinary C declarations rather than by writing descriptors by hand. core/annotate.h defines the two markers:

```c
#define NODE_TYPE(T) struct __attribute__((annotate("node:type"))) T
#define NODE_FN __attribute__((annotate("node:fn")))
```

`annotate` is a GCC attribute that Clang also implements, and libclang exposes it as `CXCursor_AnnotateAttr`. reflect/reflect.c finds it through `has_annotate()`.

The runtime needs three things from those annotations that a C program cannot ask about itself: field names, field types mapped onto `pin_type`, and field offsets. `type_desc` and `type_field` in core/type.h hold them, and generated/registry.c is where they land. For `Vec3` the output is `Vec3_fields[]` with `{ "x", PIN_F32, offsetof(Vec3, x), sizeof(float), "float" }` per field, then `T_Vec3` pointing at it. The same pass emits a Make and a Break `node_desc` per annotated struct and a call `node_desc` per annotated function, wired into `REGISTRY_NODES` and reachable through `registry_node_by_name()` in core/registry.h.

Offsets are the part that has to be right. A layout computed by anything other than the compiler that compiles the struct is a guess about padding, alignment, bitfields, and target ABI, and getting it wrong does not fail the build, it silently reads the wrong bytes.

## Decision

Reflection is a build-time tool. reflect/reflect.c is a libclang C API program invoked by a CMake custom command; it takes an annotated source file and an output path (`usage: reflect <input.c> <output_registry.c>`) and writes generated/registry.c, which compiles with the rest of the project.

Reflection is source-level, so the build compiler is irrelevant: libclang parses the annotated headers, and those same headers compile under GCC or Clang because `annotate` exists in both. The generated C emits `sizeof` and `offsetof` as source text rather than computed numbers, so the compiler that builds the project produces the real layout for the real target.

Exactly two functions touch raw struct bytes, both declared in core/type.h and defined in core/type.c:

```c
void pin_write_field(void *base, const type_field *f, pin_value v);
void pin_read_field(const void *base, const type_field *f, pin_value *out);
```

Everything else goes through `type_desc`. The Make and Break evaluators in core/nodes.c call them in a loop over `t->fields` and are the only callers.

Hand-authored descriptors stay first-class: `EXTRA_NODES` in core/const_nodes.c and `FLOW_NODES` in core/flow_nodes.c never pass through reflect, and `registry_node()` treats all three tables the same.

## Consequences

Adding a struct to the node catalog is adding `NODE_TYPE` to its declaration. Make and Break nodes appear on the next build with correct pin types and offsets, and the demo types in demo/vec3.h and demo/transform.h stay plain C structs usable outside the graph. Struct access is auditable: two functions in core/type.c are the entire unsafe surface, so a layout or type-punning bug has two places to be.

The costs are libclang as a build-time dependency, a generated file that must be rebuilt whenever an annotated header changes (the CMake custom command's dependency list is load-bearing), and an expansion policy that lives in reflect/reflect.c's C-emitting code rather than in the type system.

## Alternatives rejected

### A runtime reflection registry populated by hand-written macros

Macros can build a `type_field` list, and `offsetof` inside a macro is even correct. What they cannot do is stay in sync: the field list is written twice, once in the struct and once in the registration macro, and nothing catches a field added to one and not the other. The failure is a silently missing pin or a stale offset.

### A custom C parser

Parsing enough C to get field types right means handling the preprocessor, typedefs, nested and anonymous structs, bitfields, attributes, and every implementation-specific alignment rule that affects layout. libclang already does this correctly. A partial C parser trades a build dependency for an open-ended correctness liability in the one place, struct layout, where being subtly wrong is worse than failing loudly.
