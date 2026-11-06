# Minimal compiler facts for a material

Proposed interface for agreement with the executable compiler owner, 2026-10-22.
This is not a new public compiler API or implemented source support.

The compiler owner confirmed that current `package_compiler` has no built-in
vec3 spelling and no public fully typed expression table. It resolves types
while lowering. Shader work must not consume private Value/Name/Work records or
decode prepared bytecode to rediscover meaning.

The example uses the function form in [PLAN.md](PLAN.md) (P2), built-in
vectors (S2) and parameter types inferred from defaults (S2b). `sample()`
returns the per-sample inputs, `Surface` is the engine shader package's
result record, and unset Surface fields keep their defaults.
`number` is `f32` on the CPU and the GPU (D3), so no narrowing rule applies.

```chibi
Materials :: library {
    Gradient :: (tint = vec3(1, 0.5, 0.25), gain = 1.0, speed = 0.5) -> Surface {
        color := tint * (sample().uv.x * gain + sample().time * speed)
        return {base_color: color, emission: color * 0.125, unlit: true}
    }
}
```

This gradient is the first candidate fixture. Repeating bands follow when an
existing float math capability is verified or its domain intrinsic has an
explicit catalog contract. Do not insert an unresolved intrinsic into a working
example, and do not accept an `f64` call.

The shader pass needs these resolved facts for the reachable entry/helper closure:

| Fact | Minimal data and purpose |
| --- | --- |
| Document | Snapshot document identity/text and its immutable node IDs. |
| Range | Document index, CST node index, half-open byte range and line/column for diagnostics. |
| Type | Storage, container and stable nominal identity. Record members carry resolved field identity/type and order. No private type-table index. |
| Function | Stable callable identity, declaration range, ordered parameter identities/types, result type and body statement IDs. |
| Statement | Kind and range, lexical symbol identity for immutable locals, initializer expression or returned value. Unsupported statement kinds retain ranges for rejection. |
| Expression | Stable occurrence identity, kind, range, resolved type, ordered child IDs and referenced symbol/member/callable identity. |
| Operator/call | Resolved scalar operator or stable native intrinsic classification, callable identity, exact argument-to-parameter mapping and effects. Helpers identify their source function. |
| Literal | Exact typed constant bits or bool. Never parse literal text again. `f64` constants are rejected in the shader domain. |
| Construction/projection | Resolved field identity and expression bindings, including contextual record literals. |

All referenced IDs are snapshot facts or stable semantic identities. No runtime
registers, compiler slots or private allocation indexes cross this boundary.
The compiler validates ordinary Chibi names, imports, calls, types and contextual
construction. The shader pass checks reachable domain types/effects, rejects
unsupported statements/calls with their ranges, and lowers to the existing
`shader_graph` operations. Constants, parameters, UV/time reads, components,
multiply/add and vector construction suffice for this example.

Each helper expansion gets contextual operation IDs and retains its original
occurrence plus call-site chain. Source result bindings identify the principal
IR node separately from auxiliary-node provenance. An immutable alias can bind
an existing node. Neither graph layout nor prepared executable code defines
shader semantics.

The exact Chibi record/API names and transport envelope remain to be agreed.
Prefer a private compile-analysis result with owned arrays and source diagnostics
over widening stable public Compile arguments or changing ABI20. The compiler
owner exports facts; the shader owner implements the domain pass and existing IR
adapter. A native transport bridge may copy validated values and invoke the
authored pass; it must not resolve source semantics.
