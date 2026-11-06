# Byte conversion and UTF-8

The owner approved typed decoder result cases on 2026-10-21: a decoded value,
end of input, or a recoverable error. A caller handles the case before reading
the character. Strings remain immutable byte sequences, including embedded NUL
and malformed UTF-8; Unicode operations are explicit.

This record defines the next package discussion. The result-case model is
approved. The method names and detailed contracts below are proposals, not
implemented APIs.

## Proposed API

| Call | Result and ownership |
| --- | --- |
| `document.toBytes()` | An independent mutable `byte[]`, preserving every byte. |
| `data.toString()` | An immutable byte-preserving string copy; no implicit UTF-8 validation. |
| `document.validateUtf8()` | Valid, or an error with its byte offset and reason. |
| `document.decodeUtf8(offset)` | One decoded `char` and byte width, end of input, or an error. |

The owner prefers receiver calls such as `document.slice(...)` and
`fragment.clone()`. General authored receiver dispatch belongs to the shared
string API work; avoid a separate hard-coded dispatch scheme for UTF-8.

The decoder can use existing tagged unions. Illustrative names:

```chibi
pub DecodeError :: union {
    InvalidOffset | InvalidSequence | IncompleteSequence
}

pub DecodeResult :: union {
    Decoded(character: char, byteWidth: int)
    | End
    | Error(byteOffset: int, reason: DecodeError)
}
```

The proposed contract is:

- Offsets and widths count bytes. Decoding `"Aé"` at offset 1 returns `'é'`
  and width 2. Offset 2 starts at a continuation byte and fails.
- An offset equal to the byte length returns `End`, including zero for an empty
  string. Negative offsets or offsets beyond the length return `InvalidOffset`.
- Malformed input returns `InvalidSequence`; an otherwise valid prefix cut short
  by the end returns `IncompleteSequence`. The error points to the start of the
  undecodable sequence, following Rust's `valid_up_to` convention.
- No input or cursor is mutated. A failed decode consumes nothing. A caller
  advances its own offset only after handling `Decoded`.
- Overlong sequences, encoded surrogates, out-of-range scalars, and stray
  continuation bytes fail. U+0000 is a valid decoded character.
- Replacing malformed bytes with U+FFFD needs an explicitly named lossy
  operation. Default decoding must not silently replace input.

Whether a future iterator stops at the first error or exposes recoverable items
needs its own API decision. Existing language string iteration is not changed by
this package contract. Generic `Result` syntax and exception machinery are not
prerequisites for these package-specific result types.

## Source reuse

[Go's UTF-8 decoder](https://github.com/golang/go/blob/05dbbf9175d021c5a61a69e3cc22abbd483d7fb0/src/unicode/utf8/utf8.go)
is a concrete port candidate for its byte classification, continuation checks,
and scalar assembly. Pin source and test provenance and preserve its BSD-3-Clause
notice when porting. Go signals malformed input with a replacement character and
width 1; adapt that failure path to the approved typed result cases rather than
copying its public behavior unchanged.

[Rust's UTF-8 validation](https://doc.rust-lang.org/std/str/fn.from_utf8.html)
returns an explicit success/error result. Its
[error information](https://doc.rust-lang.org/std/str/struct.Utf8Error.html)
distinguishes an invalid sequence from an incomplete suffix and identifies the
valid prefix length. This is the proposed error-reporting precedent; Chibi's
ordinary string does not acquire Rust's always-valid-UTF-8 invariant.

## Prerequisites and ownership

Audit base: fetched engine `origin/main` at
`73a6e2b7f507186a984eb7b47cbadefab84ccb4e`, 2026-10-21.

- Both compiler stages already support tagged unions with multiple named
  payloads, imported public constructors, and switch payload bindings. Existing
  record payload support is sufficient; do not add a parallel result runtime.
- Stage zero already has a checked Unicode-scalar `char`. The local compiler
  change adds stage-one types and character literals, preserving semantic
  identity through signatures, fields, conversions, imports and result bindings.
  Emitted values retain the existing native I32 representation. Runtime checks
  reject invalid scalars before conversions and compound writes publish them.
- Typed byte arrays already work in both stages. Confirm the package's actual
  conversion and ownership paths in its tests rather than inferring them from
  syntax support alone.
- General authored string receiver methods remain a dependency of the separate
  shared-string chat. The descriptor checkpoint is draft PR 633 at
  `8b62b1cce22ee2f339b01eeecec4c952e769294d`; shared backing storage and method
  syntax are separate follow-on work. Recheck integration before package tests.

Character support is developed on `codex/utf8-foundations-20261021`, based on
fetched main, independently of draft PR 633. It does not change main's ordinary
integer arithmetic policy. Package implementation follows its settled API and
representation dependencies. Package tests must cover both compiler stages,
imported result cases, valid scalar boundaries, NUL, malformed sequences and
truncation under the repository's admitted test policy.

Two stage-one limits remain separate work: indexed compound assignment only
supports `int[]`, and nested untyped arithmetic does not fully inherit context
from a typed peer. A nested character-literal expression that needs such context
is diagnosed rather than compiled with a different arithmetic type. Explicit
integer conversion of the character supplies the intended type. These limits
do not prevent the proposed decoder's checked `char(value)` result construction.

## Local compiler validation

On 2026-10-21 the uncommitted compiler change, based on `73a6e2b7f`, passed these
admitted selectors using its own Release `test_language` driver and
`tools/test_policy.py tool`, each under the production 60-second ceiling:

| Selector | Assertions | Supervised wall time |
| --- | ---: | ---: |
| `language-package-compiler-chibi-types` | 3,242 | 8.184 s |
| `language-package-compiler-chibi-types-compiled` | 2,838 | 6.009 s |
| `language-package-compiler-chibi-bootstrap` | 10,359 | 19.097 s |
| `language-package-compiler-chibi` | 6,465 | 5.316 s |
| `language-package-compiler-chibi-collections` | 2,016 | 8.782 s |

The imported `char` record/result fixture failed before the change. Regression
cases now cover imported signatures and payloads, NUL and supplementary scalars,
character defaults and constants, contextual literals, widening, invalid
conversions, and Unicode-scalar checks on local/record/reference writes.
The narrow-integer fixture preserves this base's existing arithmetic behavior;
it will need to follow the approved overflow contract when that work lands.

The C dispatcher is formatted with clang-format 22.1.8 and `git diff --check`
passes. Build timing is separate from test timing. No bootstrap artifact was
regenerated. Local before/after and validation logs remain in
`.chibi/utf8-foundations/`. These checks validate the compiler prerequisite;
they do not claim a completed or validated UTF-8 package or string builder.
