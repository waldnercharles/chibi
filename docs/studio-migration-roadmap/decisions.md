# Current owner decisions and exact scope

Updated 15 October 2026. This decision record supersedes the unresolved-status wording of the original supplied plan only to the extent of the actual reply below.

## Approved state/self/format direction

Question sent to the owner, message `Sentinel_f0121f958b9481918ae83ae163aabdfc`:

> 2. State and self
> Use bare names normally and self.count only when a local or parameter shadows persistent count. Remove the redundant root-level state keyword through migration, but keep callable state where it changes lifetime. Format would remove redundant syntax only when meaning is preserved. Does that match your intent?

Owner reply, message `Sentinel_b1ac51df26c0819196fd97698c05c721`:

> 2. Yes.

This authorizes the described bare-name/shadowed-persistent-member behavior, root-level redundant-state migration, retention of semantically required callable state, and meaning-preserving formatting. It does not approve every row of D10's proposed receiver table, new library `self` introduction, full shared/callable qualifier precedence, import collision resolution details, named-function keyword rejection, or a broader Simplify policy. Implement and test the approved core; explicitly settle any additional user-visible semantic choice before extending beyond it. Preserve existing entity `self` meaning, all storage kinds, and actual binding identities.

## Selected inferred shared mutable captures

The owner previously selected option B, inferred captures, instead of required
explicit capture lists. On 15 October 2026 the owner also selected shared mutable
lexical binding behavior. The exact owner-conversation exchange is:

Owner, message `Sentinel_0907d3f84e0881919061135b00f86e1c`:

> I think shared mutable capture has become an expectation. Can you look into what languages like Odin, Jai, Lua, C++, C#, Typescript, and Rust do?

Assistant, message `Sentinel_f86058409e5c8191818dce1068c45acd`:

> For Chibi, two lambdas capturing count should share that variable. A write through either lambda or the outer scope should be visible to the others. Synchronous, non-escaping callbacks can use stack storage; escaping callbacks need retained shared storage. Both should behave identically.

Owner, message `Sentinel_0bc06a383f688191a6f8d96c77a268bd`, 15 October 2026 at 17:20 UTC:

> Sounds good. Mutable it is.

This selects inferred shared mutable bindings: sibling closures and the outer
execution observe the same variable, and stack versus retained representation
must not change that meaning. Immutable snapshot capture is no longer the
recommended default or an unresolved alternative for this direction. Existing
ordinary value assignment and independent managed snapshots remain unchanged;
sharing a captured variable does not turn all arrays into reference values.

The exact function-expression/type spelling, initially admitted escaping forms,
concurrency/task-producing callable admission, owner boundaries and `.chibi`
reload policy still require specification and scoped decisions. Proper capture-
cycle reclamation is now required as separate implementation work, as below;
the reclamation mechanism remains unselected.
Approval of shared mutation is not approval of a particular collector, callback
host ABI, cross-thread access, captured explicit `ref`, or old-code retention.
The [concrete proposal](../language-contracts/inferred-shared-captures-proposal.md)
provides recommendations and distinguishing examples; it implements nothing.
The [older immutable-capture proposal](../language-contracts/capture-lifetime-proposal.md)
and frozen roadmap language remain historical recommendations, not current
approval or permission to substitute snapshot semantics.

## Required cycle reclamation; syntax examples pending verification

Owner, message `Sentinel_2a838165744881918aeda4230474e503`, 15 October 2026:

> I want to handle #3 properly, but, it can be in a separate PR. For syntax verification, can you provide examples?

Item 3 was the proposal's cycle-handling choice. Proper capture-cycle support and
automatic reclamation are required in a separate implementation PR. Initial
atomic rejection can be a temporary safety limitation, not the finished language
contract or grounds for closing this requirement. This does not select a cycle
collector, reference-counting algorithm, global registry or user-managed memory.

The assistant provided proposed, unexecuted syntax examples in message
`Sentinel_4ba92fd2ffc08191b7f12fad7fd5d30f`: shared siblings and outer mutation,
a returned counter, a synchronous predicate, and eventual recursive capture.
That earlier comparison is superseded by the owner's following request; shared
mutation and proper cycle reclamation remain selected. No implementation, runtime
result or new test admission follows from providing examples.

## Requested optional lambda syntax and inferred results

Owner, message `Sentinel_d8d773ec72088191baa27679fad60dd5`, 15 October 2026 at 18:19 UTC:

> Are we able to make the fn optional for them? I think an optional arrow form would be nice as well. Along with inferred return type if possible.

Treat optional `fn`, an optional arrow form and result inference as the requested
design direction, not competing alternatives that require the owner to choose
only one. The proposal now shows `fn(value: int) { return value > limit; }`,
`(value: int) { return value > limit; }` and `(value: int) => value > limit` as
semantically equivalent expressions, each inferring a boolean result. Existing
named-function result inference is the model; unresolved recursive inference
still needs an explicit result contract. Lambda parsing/runtime support does not
exist merely because these examples are now specified.

The exact grammar and canonicalization need review before implementation. The
proposal recommends fn-free canonical expressions through semantic minimization,
including when a combined Format command performs that checked pass. The current
whitespace-only layout SDK remains token-preserving; it must not be used to
silently defeat optional-keyword removal or weaken its CST checks. Block-to-arrow
rewriting is separate and must not be implied by keyword removal.

Keep bodyless callable types distinct: the recommendation is `fn(value: int) ->
bool`, because there is no body from which to infer a result. Retaining `fn` in
that type grammar, accepting extra `fn(...) => ...` / `(...) => { ... }`
combinations, and the combined formatter's exact policy remain recommendations.
Contextual parameter inference and unparenthesized inputs are not selected.
Lifetime/owner/reload admission and the cycle-reclamation mechanism remain
pending scoped decisions; the request does not approve additional runtime scope.

## Record syntax remains open

The owner asked whether the `Name :: struct { ... }` alternative is Odin/Jai-like. That question is not a final choice between this and `struct Name { ... }`. Defaults, managed-field scope and optional C layout remain separately unresolved.

## Storage syntax remains open

The owner requested array-like syntax and asked whether the best layout could be inferred. Neither `buffer<T, aos/soa>` nor separate `aos<T>/soa<T>` is selected. Research is underway. The owning contract, borrow/resize/suspension behavior, packed representation, explicit versus inferred layout boundaries and stable ABI/reflection semantics remain unresolved. Preserve logical value semantics and avoid exposing layout inference that changes observable aliasing or ABI without a specified contract.

## Compatible work needing no new surface

Existing concrete union outcomes, library qualification, RML/RCSS/controller composition, inferred effects/ordinary borrowing and registered nominal UI events/handles can be demonstrated now. Dict/builder/import-alias additions remain evidence-triggered and need their own visible spelling choice if used. LLVM, registry, JSX and unrelated held work remain outside scope.

## Native hot-reload clarification, 15 October 2026 at 13:55

Owner message, verbatim as forwarded by the coordinator:

> Yes, this makes sense to me. Side note: I would just get rid of hot reload altogether. We don't really need it anymore and I'd argue we could simplify quite a bit without it. Unless you're talking about hot reloading .chibi files.

The parent clarified that `.chibi` reload, state and captured-callback safety remain; assess native C/shared-library hot reload separately. The opening acknowledgment is not treated as a final record or array-storage spelling choice.

B06 is a narrow audit of actual native-reload usage/dependencies and removable remnants, not a blanket deletion authorization. Current baseline [docs/reload.md](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/reload.md) already states that CUT65 removed the native development reload mode, application DLL, watcher/staging and reload-check targets. It says C changes rebuild/restart while authored assets retain live updates and checkpoints preserve workspace recovery. The subsequent code/build audit in native-reload-audit.md verified this for the exact inspected snapshots; no new deletion feature is justified. Shared source publication, generated runtime code ownership, `.chibi` state compatibility and callback invalidation are not synonymous with native C hot reload and must not be stripped.
