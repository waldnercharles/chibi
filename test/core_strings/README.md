# String and formatting regression tests

Each fixed behavior row from `test/language/package_core.c` at
`c086475d5971a762af25f3fa6395a9bff7534b7a` becomes one independent `@test`.
The tests import the production `core:strings` package. Expected values stay
literal; they are not calculated by a second string algorithm.

| Original C case or table | Authored file | Rows in source order |
| --- | --- | --- |
| `package_core_strings_comparison_identifiers_and_basename` | `Compare.chibi` | 6 Compare, 6 IsIdentifier, 6 BaseName |
| `search_cases` | `Search.chibi` | 54 |
| `text_cases` | `Text.chibi` | 54 |
| `replace_cases`, then `split_cases` | `Split.chibi` | 13 replacement, 35 split/partition |
| `native_text_rejects_unsafe_modifiers_and_bounds_placeholder_indices` in `text_format.c` | `Format.chibi` | 13 invalid modifiers, oversized placeholder, string with integer modifier, 128 literal percent bytes |

The nth test in each group corresponds to the nth original row. This preserves
all 174 string input/result pairs, including empty needles, overlapping matches,
negative limits, ASCII boundaries, UTF-8 bytes, CRLF and trailing empty fields.

Split tests compare fixed array lengths and elements as well as the original
count/joined-text oracle. Partition tests compare each field and the original
formatted text. `RawByte` checks its byte value and length; `AssertBytes` also
checks composed fixtures so formatting cannot silently remove a control or
invalid UTF-8 byte from both the input and its expected result.

The sixteen formatting rows call the public `format_text` directly and keep
fixed expected strings, including its successful fallback for malformed
modifiers. Their old native case imposed no Chibi instruction bound; these
small rows use this fixture's existing 10,000-instruction bound. Native scalar
result ownership, host-call boundaries and null-map handling stay in C.

The existing package-core selector embeds all 190 tests and runs them through
`compiler_fixture_tests_budget` on independently prepared BYTECODE and SLJIT
owners. The same discovered function identities select stage-one compiled
bodies, with each compiler backend's output loaded on both target backends.
Every complete string test keeps the former 10,000-instruction call bound.
Discovery, private invocation storage, inferred purity, worker limits and
reporting use the existing package-test runner.

Test entries end with an explicit `return;`. Stage one's implicit void tail
currently emits END, while the package-test entry contract requires RETURN.
Ordinary void helpers can fall through. This migration changes authored tests;
it does not change the compiler or relax the runner's completion check.

C retains byte-length and ownership checks, native export/purity metadata,
binding generations, retained modules, measured copy/read counts, binary
inputs and hash-collision witnesses. Those assertions require their native
inspection or independent byte oracle.
