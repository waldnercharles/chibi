# Native shader foundation validation

This PR delivers owned CPU snapshots of **supplied existing shader IR**:
intermediate values, source/call provenance, principal-result aliases, exact
ordered-document guards, nonfinite labels and transactional capture failure.
It also distinguishes sampled-SDF numeric/atlas errors from missing images.
Both SDK and Chibi build registrations include the native implementation.

The caller must pair the source map with the exact IR generation. These C
tests do not compile authored shader source or construct a new policy service.
The PR has no Analyze producer, Facts schema expansion, prepared SourceMap
service, facts-driven lowering, typed-asset finalization, authored material,
rendering or Studio change. Those future capabilities are separate work, not
claims made by this native foundation.

## Exact source and ownership

The native 17-path delta over main `d0de4a84ecf50badcab661411470bdf6f3da5fa2`
is unchanged from source commit `1c1599fa1ca2405e0ee1d6a57dc7a9146db3b252`, tree
`e4efa14d334f7dc5b6d465bf1906f77120992084`. Its merge parents preserve PR663's
original `d34def7202f842b6408f151df96ab5c7d1b343b1` and that main. There was no
conflict-resolution rewrite, compiler-producer composition or source fix after
the owner handoff. Cloud owns delivery after the explicit no-overlap handoff.
This concise note is the only additional path; historical handoff payloads are
not part of the integration delta.

## Local evidence

On Linux x86_64 with clang 19.1.7, all five CPU families pass against the exact
clean source, using a freshly built SDK and full drivers. Debug sanitizers were
**off**; projects used freshly built Release command-line tools.

| Family | Cases | Assertions |
| --- | ---: | ---: |
| Native | 647 | 237,362 |
| Materials | 110 | 270,494 |
| Language | 583 | 1,021,839 |
| Projects | 106 | 11,933 |
| Standalone tooling | 163 | 28,910 |
| **Total** | **1,609** | **1,570,538** |

The real full driver's `--shader-source-debug` separately passes 13 cases/165
assertions and adjacent `--shader-bake` passes 8/320. Those unchanged focused
suites also pass under **UBSan only**. The full C format check (then
`make check-format`, now `chibi build format --check`) passes using
clang-format 22.1.5. Independent source review found no concrete defect.

Combined ASan/UBSan builds reach the sanitized reflect generator, then this
container's LeakSanitizer fails under ptrace. A bounded combined-sanitizer
dispatcher has the same limit. No leak setting or assertion was disabled, so
combined sanitizer/leak coverage remains **blocked**, not passed. Other-platform,
GPU, Studio and installed/relocated SDK acceptance are not supplied by these
checks. Native integration must use its own exact-head CI and reviews before
leaving draft; no merge, release, deployment or ABI hold is waived here.

Full hashes, raw/normalized logs, independently reconciled counts, source and
handoff identity proofs are retained separately at
[the owned validation checkpoint](https://github.com/waldnercharles/chibi/tree/b34e5092e4ddc64afe8d0fd66f49b86ce55d2a35/docs/handoffs/2026-10-23-shader-foundation-owned).
That evidence checkpoint is not an integration dependency or extra source.
