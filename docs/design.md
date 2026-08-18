# Design: Blueprints-style node graph in C23

Working name: c-blueprints. Result is a visual node editor plus a graph
interpreter, written in C23, engine- and ECS-agnostic.

## Goal

Unreal-Blueprints-like visual scripting where node types are declared by
annotating C structs and functions, reflected with clang, and edited through
Dear ImGui.

## v0.1 scope

A standalone app that can place nodes, wire pins, and evaluate a graph. No ECS
or framework coupling. Flow/exec pins are reserved but not implemented.

## Locked decisions

### 1. Node editor

Full port of imgui-node-editor core (editor + canvas + bezier/extra math)
to C23 on dcimgui, using ckit.h (third_party/ckit/) for dynamic arrays, hash
maps, and string interning. Owned as editor/ne/. The graph, eval, and
serialization layers stay hand-written C23 (see 3, 9, 12). Undo/redo,
clipboard, mini-map, context menus, and search are not in upstream and are
built on top of the port.

### 2. Host and windowing

cute_framework hosts the app. It bundles everything needed:

- `cf_app_init_imgui()` provides the ImGui platform and renderer backend.
- `dcimgui.h` is the C binding for ImGui (same dear_bindings generator as
  cimgui), so no separate cimgui dependency.
- `samples/imgui.c` is a working pure-C reference.

### 3. Agnostic boundary

The core (graph, types, evaluation, serialization, reflection runtime) never
includes cute.h or any ECS header. cute_framework appears only in the thin host
layer (window loop and rendering).

### 4. Reflection

libclang C API, run as a build-time tool. It parses annotated sources and emits
a generated registry. Reflection is source-level: the build compiler is
irrelevant, so the same headers compile under GCC or Clang.

The tool reports names, type spellings, struct field offsets (bits), struct
sizes, and annotation strings as typed values, and emits both the `type_desc`
tables and the Make/Break/call `node_desc` tables into the registry.

### 5. Annotation

```c
#define NODE_TYPE __attribute__((annotate("node:type")))
#define NODE_FN   __attribute__((annotate("node:fn")))
```

`annotate` is a GCC attribute Clang also implements, so annotated headers
compile under both. libclang exposes it as `CXCursor_AnnotateAttr`.

### 6. Node model

A generic `node_desc` is the real interface:

```c
typedef pin_value (*node_fn)(const pin_value *inputs, int input_count, arena *a);

typedef struct node_desc {
    const char *name;
    int input_count;
    int output_count;
    pin_spec *inputs;   // name + type per pin
    pin_spec *outputs;
    void (*eval)(node *n, arena *a);  // reads inputs, writes outputs
    const type_desc *type;            // Make/Break nodes: the struct type
    node_fn fn;                       // call nodes: the adapted function
} node_desc;
```

Reflection is sugar that emits these descriptors. Hand-authored descriptors are
first-class, giving a full escape hatch for stateful or exotic nodes.

Annotated declarations expand as follows:

- struct -> Make node (fields = inputs, struct = output) and Break node
  (struct = input, fields = outputs).
- function -> call node (params = inputs, return = output).

This expansion is a codegen policy, not baked into the runtime. A later ECS
policy can emit Get/Set nodes from the same struct descriptors.

### 7. Pin types and values

```c
typedef enum {
    PIN_FLOW,     // reserved, unused in v0.1
    PIN_BOOL, PIN_I32, PIN_U32, PIN_F32, PIN_F64,
    PIN_STR, PIN_STRUCT, PIN_ENUM,
} pin_type;

typedef struct {
    pin_type type;
    union {
        bool b; int32_t i; uint32_t u; float f; double d;
        const char *str;  // PIN_STR
        void *ptr;        // PIN_STRUCT, points at the instance
        int32_t e;        // PIN_ENUM
    };
    const char *type_name;  // set for PIN_STRUCT / PIN_ENUM
} pin_value;
```

Connection rule: two data pins connect only when category matches and, for
STRUCT/ENUM, type_name matches. `pin_spec` carries the same `type_name`
(the C type spelling) so the rule can be checked at graph-build time, before
eval.

### 8. Struct type table

The runtime never touches a raw C struct directly. It goes through a generated
table, one entry per struct:

```c
const type_desc T_Vec3 = {
    .name = "Vec3",
    .size = sizeof(Vec3),
    .fields = {
        { "x", PIN_F32, offsetof(Vec3, x), sizeof(float), "float" },
        { "y", PIN_F32, offsetof(Vec3, y), sizeof(float), "float" },
        { "z", PIN_F32, offsetof(Vec3, z), sizeof(float), "float" },
    },
    .field_count = 3,
};
```

libclang supplies names, types, and offsets. The generated C bakes in
`sizeof`/`offsetof` from the real compiler. Two primitives are the only code
that touches raw struct bytes:

```c
void pin_write_field(void *base, const type_field *f, pin_value v);
void pin_read_field(const void *base, const type_field *f, pin_value *out);
```

### 9. Execution

Pure data DAG, pull-based with memoization. Evaluating an output recurses into
its inputs; a per-run map makes each node compute once. A visited set detects
cycles. Deterministic, and only the requested outputs are computed.

Flow/exec pins, branch, loops, and events are v0.2, as a separate imperative
evaluator alongside the data evaluator, Unreal-style.

### 10. Struct value ownership

- Scratch arena owns transient computed values (Make outputs, return values).
  Reset per run. No per-node alloc/free.
- The ECS owns persistent component storage. The graph reads it by pointer
  (arch_ecs storage is mmap'd and pointer-stable) and writes it by copy.
- Get/Set nodes are the boundary between arena values and ECS storage.

### 11. ECS integration

Deferred to a later module. Component-ness is read from the ECS component list
(the same `ECS_COMPONENTS` list arch_ecs uses), not a duplicate annotation. The
ECS module defines Get/Set/Add/Remove/Query nodes through the same node_desc
API. Make/Break are not replaced by this; they are the value plumbing the
entity nodes connect to.

### 12. Serialization

JSON via yyjson (MIT, no dependencies; yyjson.h + yyjson.c, vendored at
third_party/yyjson/). This is the same library
cute_framework vendors internally, but it is vendored here directly so the core
stays free of any cute_framework dependency. Human-readable, diffable, and the
most AI-editable format.

### 13. Build

CMake custom command runs the reflect tool on annotated sources and emits the
generated registry, which compiles with the rest of the project.

### 14. Editor feature target

The editor canvas should eventually reach feature parity with
imgui-node-editor (https://github.com/thedmd/imgui-node-editor):

- Node placement and dragging, pan and zoom, smooth navigation.
- Selection: single, rectangle, group drag, multi-select.
- Customizable node/pin/link rendering, bezier links, UE4-blueprint-style theme.
- Automatic highlights for nodes, pins, and links under the cursor.
- Context menus (node, link, background) and a node search/create popup.
- Shortcuts: cut/copy/paste/delete, undo/redo.
- Layout persistence (node positions saved with the graph).
- Mini-map / navigator.

2026-08-17: the editor is now a full port of imgui-node-editor core to C23
(see docs/port-plan.md), replacing incremental reimplementation. Features not
in upstream (undo/redo, clipboard, mini-map, context menus, search) are built
on top of the port.

## Parked for later

- Flow/exec pins, branch, loops, events, event entry points.
- ECS Get/Set/Add/Remove/Query nodes.
- Type coercion (v0.1 is strict same-type connections only).
- Mutable strings (v0.1 STR is const char\* into the arena).
- Dirty tracking, if profiling shows full recompute is the bottleneck.
- Multi-return and out-param functions (v0.1 is params -> single return).

## Build order

1. Reflect tool: libclang program in tools/reflect, emits type_desc and
   node_desc registry from a demo_nodes.c.
2. Core: pin_value, type_desc, node_desc, graph storage, pull-eval with memo
   and cycle guard.
3. Registry plus hand-written nodes: generated Make/Break/call descriptors and
   one hand-authored node to prove the API.
4. Editor canvas (cute_app + dcimgui): render nodes/pins/links,
   add/move/connect/delete, pan/zoom.
5. Serialization: yyjson save/load.
6. Demo: wire a graph, evaluate, show outputs.
7. Editor: full port of imgui-node-editor core to C23 (see docs/port-plan.md),
   then undo/redo, clipboard, mini-map, context menus, and search on top.

## Module layout

- core/ - node_desc, pin_value, type_desc, graph, evaluation, serialization.
  No runtime deps beyond libc + yyjson.
- reflect/ - the libclang tool that emits the registry.
- editor/ - the imgui-node-editor C23 port (editor/ne/) plus the bridge to the graph model.
- app/ - cute_framework host loop.
- generated/ - build-time output from reflect/.
- third_party/ - vendored deps: yyjson/ (JSON), ckit/ (dynamic arrays, hash
  map, string interning).
