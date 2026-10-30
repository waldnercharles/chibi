# Abstraction, control flow and functions

Treat every abstraction as a cost that must buy something nameable. Architecture findings here are `ARCH` or `SIMPLIFY`.

## Abstraction

- **Compression order.** Was the abstraction derived from working concrete code, or designed before any code used it? Is there usage code (a test, scene or caller) showing how it is called?
- **Operation first.** Is one operation spread across per-kind files or types, so the pattern across kinds can't be seen? One procedure per operation, switching over the kinds, keeps that pattern visible and makes table-driven simplifications possible.
- **Generic versus specific.** Is the generic machinery larger than the specific cases it replaces? Specific code is usually smaller.
- **Layer count.** How many layers sit between the caller and the work, and what does each one do? A wrapper, manager, adapter or registry that only passes calls through is a cost; could the two pieces just call each other? Compare the net line change with the behavior added.
- **Transforms.** Does a pass read its input without changing it and produce a new output, or does it mutate shared state and globals? Is a stateful visitor used where a stateless per-node transform would do?
- **Effective codepaths.** How many distinct cases does each call site now have to handle? A change that multiplies the cases callers see costs more than its line count suggests.
- **Kinds versus flags.** Is a new kind defined where a combination of feature flags on one kind would do, or the reverse? Flags suit widgets and UI boxes; tags suit compiler nodes; table membership suits high-count runtime state.
- **Second caller.** Does each new helper, interface, option, parameter or module have two real callers today? Name them. With one caller, inline it. A generic container, type parameter or interface waits for three users.
- **Mode flags.** Was a `bool` or mode parameter added to a shared function to switch its behavior? *Fix:* a new layer on top, which leaves the lower call intact for its existing callers.
- **Generic parameter, different structures.** Does one generic or mode parameter choose between what are really different data structures? *Fix:* separate types.
- **Dispatch over a closed set.** Is a vtable, callback table or function pointer standing in for a `switch` over a closed set of kinds? Function pointers belong only at allocator, OS, I/O, task and user-extension boundaries.
- **Frameworks.** Does every system now have to adopt a base type, reflection tags or a central serializer to take part? Does a per-item indirect call sit inside a loop over many items?
- **Downstream state.** Must a caller mirror its own state into a downstream system (render, UI, audio) through create and destroy calls, where passing the state each frame would do?
- **Callbacks.** Would an array or event list returned after the step do instead of a callback? A callback that must stay takes a user-data argument and must not mutate the structure it is traversing.
- **Wrong merges.** Were two look-alike blocks merged although they behave differently? In the other direction, does a long, intricate duplicate risk a fix landing in only one copy?
- **Rare-case generality.** Does a general path exist only to serve a vanishingly rare case? *Fix:* make the common case direct, and give the rare case its own slow path.
- **Contagious generics.** Does a generic interface force every caller and container to become generic, where one concrete hot path would do?

## Control flow

- **Exhaustive switches.** Is a `switch` over an internal kind missing cases without a deliberate opt-out? Is there a `default` arm that doesn't assert it is unreachable?
- **Early-outs.** How often is each early-out taken? If rarely, would running the full work every time be simpler and just as fast?
- **Hidden control flow.** Does control move through a callback, implicit registration, sugar that branches, or a non-local exit?
- **Searchability.** Can every caller be found with grep? Dispatch keyed by a string breaks that.
- **Implicit state machines.** Do interacting `bool`s encode a state machine? *Fix:* name the states.
- **Debuggability.** Does a compact expression hide an intermediate value you would want to inspect or break on, as nested ternaries do?
- **Recursion depth.** Can recursion depth grow with untrusted input until the stack overflows? Recursive descent over bounded nesting is fine.
- **Partial state.** Does an error path continue with partly updated state?

## Functions

- **Long functions.** Is the procedure a lexer or state machine, a `switch` over a closed set, or a pipeline of commented blocks? If so, its length is fine. If not, does it hide sections that deserve their own names? Length alone is never the finding.
- **Fragmentation.** Was single-use sequential code split into helpers, so the reader has to rebuild a call graph to follow one operation?
- **Threaded state.** Is the same group of values passed into and returned out of several procedures? *Fix:* one state record passed by reference. In the other direction, were unrelated arguments bundled into a record only to shorten a signature?
- **Category returns.** Does a helper return a category the caller must branch on, where a value would compose (a signed distance rather than inside/outside/touching)?
- **One where there are many.** Does a function take one object when every caller has many? *Fix:* take the array, and loop inside.
- **Guessing functions.** Does a function guess the state of its input (`ensure_*`, "create if missing") or serve two callers who want different things?
- **Boolean arguments.** Is a call site unreadable because of positional `true` and `false` arguments? *Fix:* use named constants, or a definition record.
