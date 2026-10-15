# Current owner decisions and exact scope

Updated 15 October 2026. This decision record supersedes the unresolved-status wording of the original supplied plan only to the extent of the actual reply below.

## Approved state/self/format direction

Question sent to the owner, message `Sentinel_f0121f958b9481918ae83ae163aabdfc`:

> 2. State and self
> Use bare names normally and self.count only when a local or parameter shadows persistent count. Remove the redundant root-level state keyword through migration, but keep callable state where it changes lifetime. Format would remove redundant syntax only when meaning is preserved. Does that match your intent?

Owner reply, message `Sentinel_b1ac51df26c0819196fd97698c05c721`:

> 2. Yes.

This authorizes the described bare-name/shadowed-persistent-member behavior, root-level redundant-state migration, retention of semantically required callable state, and meaning-preserving formatting. It does not approve every row of D10's proposed receiver table, new library `self` introduction, full shared/callable qualifier precedence, import collision resolution details, named-function keyword rejection, or a broader Simplify policy. Implement and test the approved core; explicitly settle any additional user-visible semantic choice before extending beyond it. Preserve existing entity `self` meaning, all storage kinds, and actual binding identities.

## Selected inferred captures, semantics still to specify

The owner selected option B, inferred captures, in response to the explicit-versus-inferred question. This preference supersedes the plan's explicit-list recommendation. Do not implement explicit lists as the required default.

This does not by itself select the function-expression signature syntax, by-value versus reference inference, mutation behavior, borrowed/owned type spelling, escaping/retained callbacks, owner/reload policy or task-producing closures. Safe recommendation to evaluate: infer immutable by-value capture and require explicit shared mutation, with inferred capture names/types/costs visible in tooling. The requested choice must be reconciled with real current parser/compiler capabilities before code changes.

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
