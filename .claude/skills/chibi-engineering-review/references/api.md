# APIs

Public API deserves stricter scrutiny than internal code, because it freezes. Internal signatures inside a package change freely, and callers get fixed.

- **Batches.** Is there a per-item call where the consumer naturally takes a batch? Does an I/O layer copy where it could lend a pointer into its buffer?
- **Allocation hooks.** Does an allocation hook take a context pointer and the sizes it needs? Does an arena-backed API imply that freeing individual objects works?
- **Natives.** Are natives registered once with a type signature, arguments borrowed and results owned? Do they take unboxed scalars?
- **Locked representation.** Does the API expose an access pattern that locks in today's storage layout?
- **Need.** Is the operation actually needed publicly? Which caller needs it today?
- **Leaked representation.** Does the API return a pointer to an internal object instead of a handle, or expose internal layout?
- **Definition records.** Does creation take a definition record with valid defaults, copied at creation and never retained?
- **Integration cost.** How many steps does it take from import to the first working call? Must the user register the same thing in several places?
- **Common case.** Is the simple case one call? Can each convenience call be rebuilt from two to four lower-level public calls?
- **Entry-point growth.** Do entry points multiply, one per variant, type or encoding, where one entry point with a parameter or record would do?
- **Frozen semantics.** Does a getter, setter or global expose semantics you won't want to support forever? Could the value be passed per call instead?
- **Hidden coupling.** Are there ordering requirements between calls, grab-bag structs, fields with only one legal value, or sibling calls with inconsistent error conventions?
- **Retained mirroring.** Must the caller mirror its own state into the component? Would an immediate-mode form remove that duplication?
- **Setters.** Does a setter duplicate a definition field for a value that never changes, or hide an expensive operation behind field-like syntax?
- **Frozen types.** Was each public size, index and handle type chosen deliberately, knowing it freezes once used?
- **Call-site clarity.** Are ownership, lifetime, units, mutation and failure behavior obvious at the call site? Is it clear what may be `none`?
- **Surprise cost.** Can a caller trigger unexpectedly large work, such as a full rebuild behind an innocent-looking setter?
