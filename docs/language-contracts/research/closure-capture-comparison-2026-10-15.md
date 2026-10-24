# Closure capture semantics and a Chibi direction

Research date: 2026-10-15. Design research only; no language change is approved or implemented by this note. Live primary documentation and implementation sources were checked. ECMAScript and C++ links are living drafts; runtime implementation observations are not universal language guarantees. Jai remains an explicit evidence gap.

## Recommendation

Choose **inferred capture of lexical variable bindings**, including ordinary mutation, as the proposed Chibi author-facing contract. This matches JavaScript/TypeScript, Lua, and C# expectations. Keep allocation, escape analysis, and environment representation separate from that meaning. A compiler can implement a shared binding on the stack for synchronous nonescaping uses and preserve it in owned storage when it escapes.

This is a design recommendation, not an implication of the earlier decision to infer captures: inference answers *which names*; binding/value capture answers *what they mean*; ownership answers *how long they remain valid*.

## One discriminating example

Language-neutral pseudocode, not proposed Chibi syntax:

```text
make_counter():
    count = 0
    increment = lambda: count = count + 1; return count
    read = lambda: return count
    count = 10
    return increment, read

increment, read = make_counter()
read()       => 10
increment()  => 11
read()       => 11
```

The desired shared-binding result is 10, 11, 11 after the declaring scope ends. A second call to `make_counter` must create a separate counter. Two closures over one declaration instance share a binding; two declaration instances do not.

A value-snapshot implementation instead gives each closure its own creation-time value: the reader remains 0 and the incrementer changes its private 0 to 1. Keeping a referenced object mutable does not make this shared-variable semantics: object mutation and rebinding the variable are separate tests.

## Language comparison

### JavaScript / TypeScript

Captures are inferred. Functions retain access to the lexical binding, so the example produces the shared result, including after return. TypeScript documents both subsequent outer assignment and modification through a returned function. `let` is block-scoped and loop declarations create per-iteration bindings; a function-scoped `var` loop can give all callbacks the same binding. `const` prevents rebinding, not mutation of the referenced object. [TypeScript handbook](https://www.typescriptlang.org/docs/handbook/variable-declarations.html)

ECMAScript creates functions with their lexical environment. Its Environment Records specify observable bindings, not mandated physical heap records; engines may optimize their representation while preserving behavior. [Function creation](https://tc39.es/ecma262/multipage/ecmascript-language-functions-and-classes.html#sec-runtime-semantics-instantiateordinaryfunctionexpression), [Environment Records](https://tc39.es/ecma262/multipage/executable-code-and-execution-contexts.html#sec-environment-records)

Typical costs are function/context objects and GC retention of captured state. Allocation elimination is an implementation opportunity, not a source-language guarantee: V8 explains how escape analysis can replace nonescaping objects with stack/register values. That article is historical mechanism evidence, not a current benchmark. [V8 explanation](https://v8.dev/blog/disabling-escape-analysis)

`await` suspends and later resumes an execution context; it does not snapshot captured variables. An operation resuming after another callback modifies a binding observes the later value when it reads it. Async interleaving still needs program-level coordination. [ECMAScript Await](https://tc39.es/ecma262/multipage/control-abstraction-objects.html#await)

### Lua

Captures are inferred lexical upvalues. Closures over the same local share that variable; each execution of a local declaration creates a fresh binding. The counter example therefore works after return. A captured table binding can be reassigned, and table contents can also be mutated; these are distinct operations. Lua coroutines cooperate through explicit yield/resume rather than supplying implicit atomicity. [Lua 5.5 manual, lexical scoping and coroutines](https://www.lua.org/manual/5.5/manual.html#2.2)

The reference implementation makes the optimization boundary concrete: open upvalues point at stack slots; closing an upvalue relocates its value into the upvalue object and redirects access. Closures sharing a slot reuse the same upvalue. Closure/upvalue objects are GC-managed, so “open on stack” does **not** mean the reference interpreter has eliminated closure/upvalue allocation. [Lua 5.5 implementation, lfunc.c](https://www.lua.org/source/5.5/lfunc.c.html)

### C#

Captures are inferred references to the same outer **variable instance**, including value-type locals such as integers. The counter example works after return; sibling delegates share changes. The specification extends the variable's lifetime and illustrates hoisting locals into fields of compiler-generated classes. That illustration is explicitly non-mandatory. [C# specification, outer variables and implementation example](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/expressions#12226-outer-variables)

A `static` lambda rejects capture. An outer `in`, `ref`, or `out` parameter cannot be directly captured; `ref` locals and `ref struct` values also have capture restrictions. These prevent stack-lifetime references from being smuggled into long-lived closure storage. [Lambda reference](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/operators/lambda-expressions), [lambda diagnostics](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/compiler-messages/lambda-expression-errors), [ref structs](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/builtin-types/ref-struct)

Typical lowering allocates a display-class environment and a delegate, retaining reachable values for GC. This is not necessarily boxing an integer into `object`: an integer can be a typed environment field. Current .NET documentation demonstrates escape-analysis elimination/stack allocation for suitable delegates and closures. Exact results depend on runtime, inlining, and escape visibility. [ .NET 10 performance](https://devblogs.microsoft.com/dotnet/performance-improvements-in-net-10/)

Async lambdas can retain the same captured binding; they do not make shared mutation synchronized. Lifetime safety and concurrency safety must be distinguished. [C# async lambda and capture documentation](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/operators/lambda-expressions)

### C++

Capture is explicit through a list or a chosen default: `[x]`/`[=]` copy; `[&x]`/`[&]` reference. The default infers the needed names, not the copy/reference policy. Reference captures share outer mutations, but capturing an automatic local by reference does not extend its lifetime. Returning the example's reference-capturing lambdas leaves dangling access. Copy captures create closure members; `mutable` permits modifying those private copies in an ordinary lambda. It is unnecessary merely to modify a referent. Two by-value lambdas do not share an integer counter. Captured pointers still address the original objects. [C++ capture rules](https://eel.is/c++draft/expr.prim.lambda.capture), [closure type rules](https://eel.is/c++draft/expr.prim.lambda.closure)

A concrete closure is a class object and need not allocate dynamically. Copy/move/destruction costs follow captured members. Safe escaping shared state can be explicitly owned, for example by capturing a shared owner. Type erasure through `std::function` can add allocation; the standard recommends avoiding it for small callables but does not guarantee that for every lambda. [std::function constructors](https://eel.is/c++draft/func.wrap.func.con)

Coroutine lambdas introduce another lifetime trap: even by-value captures can die with the closure before the suspended coroutine finishes. The Core Guidelines recommend avoiding capturing coroutine lambdas; owning coroutine parameters are a safer pattern. [CP.51](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp51-do-not-use-capturing-lambdas-that-are-coroutines)

### Rust

Capture mode is inferred from use: shared borrow, mutable borrow, or ownership, with a special unique immutable borrow in some reference cases. `move` forces value capture, copying `Copy` values and moving others; it does not deep-copy objects. Inference does not automatically choose heap ownership because a closure escapes. [Rust closure expressions](https://doc.rust-lang.org/reference/expressions/closure-expr.html)

The unmodified counter example cannot simultaneously retain a mutable-borrowing incrementer and a reader borrowing the same integer, nor return borrows of that local. A single mutating closure is fine within its borrow lifetime. `move` over an integer gives independent copies, not shared counters. Borrow checking rejects unsafe escape. [Rust book, captured references](https://doc.rust-lang.org/book/ch13-01-closures.html)

Closures have anonymous struct-like types; concrete closures require no intrinsic heap allocation. All implement `FnOnce`; non-consuming closures may implement `FnMut`, and non-mutating/non-consuming ones may implement `Fn`. These classify calling capability, not a one-to-one mapping to capture mode: a `move` closure can implement `Fn`. Capture precision can avoid retaining an entire aggregate. Async closures can lend captures to their futures and have additional call-trait restrictions; ownership and borrow validity continue across suspension. [Rust closure types](https://doc.rust-lang.org/reference/types/closure.html)

Explicit shared ownership/interior mutability provides the Lua-style result: cloned `Rc<Cell<i32>>` handles suffice for a single-threaded integer; `Rc<RefCell<T>>` handles general mutable access with runtime borrow checks. `Rc` adds allocation/reference-counting and requires weak edges or another policy for cycles. Cross-thread shared mutation generally needs `Arc` plus synchronization or atomics; `Arc` alone does not make arbitrary mutation safe. [Cell](https://doc.rust-lang.org/std/cell/index.html), [Rc](https://doc.rust-lang.org/std/rc/index.html), [Arc](https://doc.rust-lang.org/std/sync/struct.Arc.html)

### Odin

Odin's official FAQ says it has only non-capturing lambda procedures. Nested/anonymous procedures are therefore not evidence of ordinary lexical closure capture. The counter factory cannot be expressed as an inferred lexical closure. Explicit state objects and parameters are the relevant comparison, with lifetime and allocation chosen for that state rather than supplied by closure semantics. [Odin FAQ](https://odin-lang.org/docs/faq/#does-odin-have-closures)

### Jai

**Current capture semantics not verified from primary authoritative material.** Searches found community descriptions and old primers, but no current official language reference or compiler-author statement sufficient to establish the requested contract. Neither nested procedures, lambda syntax, nor metaprogramming capture establishes runtime lexical closures. Do not assign Jai inferred mutable capture, snapshot capture, or definite non-support on this evidence. A verified current distributed compiler/how-to example or author statement is needed before making Jai a design precedent.

## Chibi design consequences

The following are proposed conclusions drawn from the comparison and the checked local contract documents, not claims that Chibi already implements these features.

1. **One logical storage identity.** Every closure and the enclosing execution referring to a captured mutable declaration must access the same binding. Creating two separate detached snapshots breaks the counter example. Transitive captures through nested lambdas must resolve to the same identity.
2. **Cheap local callbacks first, without changing their meaning.** A compiler-proven synchronous nonescaping comparator/filter can use stack slots or a stack environment, an environment argument, and direct calls/inlining. Mutating captures do not inherently require a heap. An enforced nonescape contract is useful; purity or a blanket ban on writes is not required by shared-binding semantics.
3. **Preserve writes through aliases.** A read-only lambda can observe a variable that another closure or the outer scope changes. Optimizing its capture to a snapshot requires proving those changes unobservable. Under shared semantics, mutable aliasing is part of the contract rather than automatically a reason to prohibit capture.
4. **Owned storage for escape.** Returning, storing, enqueueing, or suspending a callback requires storage whose lifetime covers every holder. Options include eager environment allocation, promotion of stable cells, or owner-scoped regions. Choose after examining Chibi's runtime ownership model; none requires asking ordinary authors to free a closure. Promotion must preserve identity and account for stack references, paths, and reentry.
5. **Do not conflate shared bindings with shared arrays.** Chibi can retain its value/managed-copy rules for ordinary assignment while multiple closures access the same variable cell. Copying an array out of that cell may still create an independent value. Rebinding the cell must be visible to every sibling; existing independent snapshots remain independent. Large managed payload copy/retain/release costs must be measured separately from environment allocation.
6. **Make escape costs inspectable.** Distinguish no environment, stack environment, owned environment, and managed payload copies in compiler diagnostics/profiling. Count allocations, retained bytes, copies, retains/releases, and callback dispatches; do not promise a fixed allocation per capture or zero cost before measurements. Capture only needed bindings, group compatible lifetimes, and split environments when otherwise a tiny callback retains large unrelated data.
7. **Suspend safely.** A callback or task live across a yield cannot borrow an expired C stack slot or stale ECS/component address. Retain stable ownership and reacquire checked paths. Host reentry and structural mutation need an explicit validity model. Reject a presently unsupported boundary with a precise diagnostic, rather than changing all captures into immutable snapshots.
8. **Reload is an ownership/code problem, not a mutability argument.** Code identity, environment layout, owner generation, and resource generation must remain compatible. Plausible policies are invalidation/re-registration or pinning an old code/environment generation; neither is selected here. Never silently invoke new code against an old layout. Generation checks are still necessary for stale resource handles even when captured bits are immutable.
9. **Handle cycles and concurrency explicitly.** Environment-to-closure cycles need a defined reclamation strategy. Shared mutable captures alone promise neither race freedom nor atomic compound updates. A single-thread/event-loop default can be simple, but any cross-thread feature needs separate rules.
10. **Keep existing identity concepts separate.** Named-function capture restrictions, persistent callable state, and entity-owner `self` are existing independent contracts. Adding lexical lambdas should not silently alter them or make lexical locals persistent across independent calls.

## Suggested acceptance cases before implementation approval

- Outer write after creation; lambda write observed outside; sibling mutation; nested transitive capture.
- Counter factory after return; independent factory instances; fresh loop/block bindings; shadowing.
- Scalar, string, array, nested record, object/resource handle: test rebinding separately from content mutation and ordinary value copies.
- Nonescaping fast path versus owned escape produce identical observations, including reentry and failure behavior.
- Suspension/resume, owner destruction, reload invalidation/pinning, cancellation, and cleanup after failed construction.
- Closure copy/assignment/drop, self-referential environments, independently retained snapshots, and bounded reclamation.
- Exact bytecode, native JIT, and Wasm results under the project's normal test controls; benchmarks on the real sort/filter consumer and a retained UI callback consumer.

## Existing Chibi proposal reviewed

Local working tree: `chibi-language-semantics/docs/language-contracts/capture-lifetime-proposal.md` and `v1.md`. The proposal marks immutable snapshot capture as unapproved and unimplemented. The source-backed contract says explicit task references can share attached locals and retain detached owned snapshots; this is not itself an approved lambda rule. This note recommends revising the proposal's default, not implementing that revision or editing existing contracts without the coordinating decision.
