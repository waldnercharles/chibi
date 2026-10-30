# Style

Run this pass last. Every `STYLE` finding names its cost to the reader. Skip anything the formatter decides.

- **Filler names.** Is there a filler or redundant name (`Manager`, `Data`, `Info`, `Util`, `Helper`, `Misc`), or a repeated namespace (`json.JsonValue`)?
- **Meaning.** Does the name state the meaning rather than the type or the implementation? Is one concept named the same way everywhere?
- **Scope.** Does name length match scope? Short names belong in small local scopes; wide scopes need domain names.
- **Magic values.** Is there a raw byte value or magic number where a character literal or a named constant belongs?
- **Units and nullability.** Are units and the possibility of `none` stated in the name or in a field comment?
- **Doc comments.** Does a doc comment restate the signature instead of the contract? Does it say *assume* for an unchecked invariant and *assert* for a checked one?
- **Stale comments.** Does a comment describe behavior the code no longer has?
- **Integer types.** Is an unsigned type used to "enforce" non-negativity in code that subtracts? Are there signed/unsigned mixes, narrowing conversions or overflow assumptions?
- **Sized strings.** In C, are parsed strings handled as sized views, without `strlen`, `strtok`, `strto*` or `ctype.h` on parsed input?
- **Signed sizes.** Are sizes and counts signed, with ranges checked before computing (`count > (end - beg) / size`) rather than relying on unsigned wraparound?
- **Vague verbs.** Is a function named `update`, `process` or `handle` where a precise verb fits?
- **Layer names.** Does each symbol's name show which layer owns it?
- **Precedence.** Would a mixed-precedence expression send the reader to the precedence table? Add parentheses.
- **Ranges.** Is a range written in a form that invites off-by-one errors? Is a size or count calculation repeated in several places? Is there pointer arithmetic without an obvious bound?
- **Initialization.** Is state uninitialized on some path?
- **Casts.** Is there an unnecessary cast, or one that hides a type mismatch?
- **Boolean arguments.** Are there positional `bool` arguments that are unreadable at the call site?
- **Temporaries.** Is there a temporary that adds nothing? Keep it when its name explains the algorithm.
