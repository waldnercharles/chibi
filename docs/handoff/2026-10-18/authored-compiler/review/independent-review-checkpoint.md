> Historical technical record, with coordination-only wording removed. Read the top-level README.md for current dependency heads, publication status, omitted reference copies and the parser admission update. Historical source-only status and earlier hashes describe preparation, not a passing build or runtime result.

# Independent review checkpoint

Review paused on 2026-10-18, before any build or test,
for the requested commit/push handoff. This checkpoint authorizes no publication,
merge, full compiler execution, or change to dependency holds.

## Completed: parser-only admission

ADMIT one separate build and one complete supervised invocation of the exact
18-file syntax-precheck packet. Its frozen.sha256 SHA256 is:
`a2d920d9d4f3745f2071499aa038216d1ab9f6b45694fee04f94948399957149`.
This review performed no diagnostic build or execution. No build/test will start before handoff.

Independent read-only verification completed:
- All frozen packet and installed toolchain hashes verified.
- All 18 embedded decimal arrays decode to the corresponding complete source
  files plus exactly one NUL: 179,267 bytes total, largest 30,602 bytes.
- New authored corpus files equal the candidate sources. Corrected Lower equals
  the separately reviewed candidate, SHA256
  `1778dd60224e4edb46e87d4563e20e0625b170da933976c317f00340eecfd4fd`.
- Parser, scanner, runtime, pico and reference inputs match diagnose-scalar
  byte-for-byte. Production supervisor is unchanged, SHA256
  `d6e9085dff6a6e0771f3bb8ab8afcfc4be64bffc7cbaaf0e51c65740e50055dd`.
- The inspect function and preceding C logic are byte-identical to the earlier
  independently reviewed harness. Delta is the explicit 18-case corpus and
  sole selector. There is no source discovery or subset route.
- Each parse admits a nonempty source strictly below 32,768 bytes. One parser,
  tree and cursor are alive at a time. The unchanged post-parse 50,000-node
  guard limits traversal, not parsing allocation. At most 900,000 visible nodes
  and 2,304 numeric diagnostic location lines across all cases.
- One C workload command, no authored/VM/JIT execution, unchanged shared
  production 60-second supervisor, no splitting or retries. Separate installed
  GCC build uses section GC and no sanitizers; run checks the built binary hash.

The prior independent lifetime/error-path findings were reused for
unchanged code. Repository AGENTS.md and relevant docs/testing.md were read.
A parser pass would establish syntax only, not semantics or runtime correctness.

## Substantive compiler review: unfinished

Candidate patch SHA256:
`1b6db1fa96f49358d4bf05c4e8ed8d3533900142a1782f7c7d460c0dc9e19924`.

Read the root handoff, current manifest, compiler contract document and all
three authored source files (Compile, Expression, Artifact). No completed
correctness conclusion or new concrete compiler defect was established before
the stop request. Reading these files is not independent approval of them.

Still unreviewed: native producer/fixture implementations, full C test workload,
registration/integration changes, detailed cross-component semantic/ABI and
ownership analysis. The advertised 78 Compile calls, 106 parses, 28 payload
callbacks, seven bridge rejection checks and 56 target invocations have not
been independently audited in this review.

The package remains source-only and dependency-gated. No exact combined
production tree exists here. Dependencies' affected runtime checks and current
holds/protections remain in force; this checkpoint does not substitute for
those checks or the unfinished substantive compiler review. Fresh host API
instances are same-process consumers, not separate executable processes.
