# Bounded text ownership follow-up

2026-10-25; source `f4458b21f67badda787970218e3f8058d44cccf9`, with the
reviewed diagnostic checkpoint `3a84078a48c43ba1c32fc9a1e8310411af793140`
and this follow-up's read-only cleanup-size accessor and tests. No text,
collection, cleanup or GC behavior is changed. Libraries are isolated to this
profiling checkout. The previous public copy-count/scope extension still
requires rebuilding **every linked consumer**; this is not a binary SDK or
schema integration.

## Reporting and author-evidence correction

The first published probe receipt says **0.27 s process elapsed**, **0.371 s
policy wall time**, **2,080,768 bytes maximum RSS (1.98 MiB)**. The initial
handoff used 0.07 s from a separate uncommitted local revalidation log; that is
not the run in the published receipt. Original raw receipts are preserved.

Root supplied clarified author evidence from PR694 comment **6391727661**:

- The author's baseline was `7a82e552fa279c2aac51f0ce622e212b702ab6dd`:
  3,715,744 kB / 252 s / rc=1 at load failure.
- f445's last listed VmRSS was 10,680,532 kB at 330 s. The sampler stopped it;
  this is **not a terminal peak or established OOM**.
- Temporary uncommitted instrumentation reported 2.2 GB value allocation and
  1.34 GB cleanup allocation in a sampled window, plus adoption 225 MB,
  reserve 139 MB and approximately zero CoW clones. These are author leads,
  not independently reproduced dominance measurements.

No full compiler run was attempted here. No parked GC prototype was adopted;
native re-entry safety is unresolved. f445 and `537d400b` are siblings over
7a82. The latter's 14 added lines in `src/execution/function.c` skip scalar
ownership scans; they are a separate input difference, not part of f445 or
this checkpoint.

## Tiny production-path measurements

Two pico_unit cases, **24 scenarios / 1,416 assertions**, use the existing
`test_tools --text-ownership-probes` dispatcher. Each scenario makes 4 or 8
calls, either keeps all results or discards all but the last, and uses an
ordinary or collectable destination arena. Three routes are measured separately:

1. Owned `pin_value_text_slice`: shares the backing, creates a descriptor.
2. `EXECUTION_value_copy_call.value`: the production VALUE-copy intrinsic,
   sharing the owned text backing through the collection-copy path.
3. `NATIVE_TEXT_SLICE_CALL.value`: the native called by core `strings.Slice`,
   which copies selected bytes through `pin_value_text`.

The owned producer is ten bytes, `{'a','b',0,'c','d','e','f','g','h','i'}`.
The selected value is checked against the independent four-byte literal
`{'b',0,'c','d'}`, using length and `memcmp`, before and after producer
arena destruction. VALUE-copy takes an already-owned producer window. Every
retained value is checked again after rooted collection, where supported.

| Per scenario, immediately after calls | 4 calls | 8 calls |
| --- | ---: | ---: |
| Shared slice or VALUE-copy: unique backing payload, including trailing NUL | 11 B | 11 B |
| Shared slice or VALUE-copy: unique external allocation, including owner | 27 B | 27 B |
| Descriptor occupancy in either arena on this host | 96 B | 192 B |
| Separate ordinary-arena cleanup nodes | 4 / 128 B | 8 / 256 B |
| Core native slice: copied bytes, including trailing NUL | 20 B | 40 B |
| Core native slice: unique external allocations | 4 / 84 B | 8 / 168 B |

Discarding all but the last result has the same pre-retirement footprint as
keeping them all. `pin_value_collection_release` is called on discarded
results; that API visits collection owners and does not release scalar text.
Text ownership remains attached to the arena descriptor. This is **not a test
of VM call-frame/drop dispatch**; direct `EXECUTION_value_drop_call.value` is
intentionally a no-op and is not used as a substitute.

At 8 shared calls, ordinary-arena descriptor occupancy plus separate cleanup
nodes is **192 + 256 = 448 B**, versus **11 B of unique payload**. This explains
how small shared strings can generate allocation work without copying their
bytes. It does not establish the dominant compiler call site or lifetime.
Ordinary arenas retain that work until reset/free; there is no ordinary-arena
collection attempt.

For collectable keep-last scenarios, after producer destruction and safe rooted
post-call collection:

- Shared slice/VALUE-copy descriptor occupancy falls **192 → 24 B**, retaining
  the same **27 B external owner / 11 B payload**. `storage_bytes` stays 1,179 B
  because retired descriptor allocations are reusable; it falls to 960 B after
  rootless collection while still retaining reusable arena storage.
- Native core slice descriptor occupancy also falls **192 → 24 B**, while
  external allocations fall **168 → 21 B**, payload **40 → 5 B**.
- Keep-all retains all eight descriptors. Final rootless collection or ordinary
  reset leaves zero occupied bytes, resources and separate cleanup nodes.

A shared small view retains the original backing's full payload: here 11 B,
versus the native copied result's 5 B after keep-last collection. Therefore
changing every native slice to a shared view is not justified by copy counts
alone. No such optimization is included.

`descriptor_used` is arena occupancy excluding unique external resources;
ordinary-arena alignment padding may contribute on other targets. This pure
fixture stores no other destination objects. `payload_unique` is the observed
resource count times the known equal backing lengths for this fixture, not a
generic payload profiler. `external_unique` already includes backing metadata.
`arena_used`/`arena_capacity` include external resources once; do not add them
again. Producer allocations and transient ownership-traversal work are not
included in the destination snapshots. `storage_bytes` includes allocation/index/cleanup metadata and reusable
capacity. `arena_cleanup_bytes` measures only separate ordinary cleanup nodes;
collectable allocations embed cleanup fields and report zero here. None of
these quantities is RSS, and reducing live occupancy does not imply equal
physical-memory reclamation.

## Main957 and M2 comparison

Read-only source snapshots:

- Accepted main: `957968675031431c14b1abf1b5f8823aebd71097`.
- Latest observed local M2 checkpoint:
  `b1fcb5e006d8e2e16b4a39dddff6aafbd92075d1`, branch
  `codex/m2-pointer-float-fixture-20261025`, including reconciled compiler source
  `a01f8fdb816dd337f6f08cfaf37072c65526bf32`. This identifies the comparison
  snapshot; it is not a claim about a moving branch's future tip or acceptance.

[Exact blob identities](text-source-blobs.txt) were recorded with
`git rev-parse <commit>:<path>` in the existing local repositories. No compared
source was imported into the diagnostic branch.

| Path/pattern | Result |
| --- | --- |
| `src/runtime/text_format.c` | Identical blob `a50387a0e490693f2bef5b5042227fa1fd86baba` at all three sources. Native `text_slice` still clamps and copies through `pin_value_text`. |
| `src/types/text.c` | Main957 and M2 identical (`5e0e5c064b10ef20a6c4053a65635ac3c90596fa`): older descriptor-plus-bytes allocation. f445 has refcounted text owners/share/slice support. Those implementations are not interchangeable binary libraries. |
| Core `Strings.chibi` | f445 and main957 identical (`4134f878861d403806a607ca773f440d1a7a98ca`). M2 adds integer conversion changes, not a new substring ownership fix. |
| Count / SplitKeeping | Commit `79ea415beeb2aeaa8a0ea7631b58c664949829ad` already replaced repeated suffix slicing with original-string offsets/IndexFrom and removed unlimited split pre-count. Present in all three; do not reimplement it. Returned split pieces still use Slice. |
| LastIndex | Hash-hit equality checks still use `Slice(s,i,n)`; bounded source finding, not a measured hot spot. |
| Stored byte length | PIN_STR byte length/ByteAt use the stored length. Do not repeat an older `strlen`-per-byte diagnosis; PIN_NAME retains a `strlen` fallback. |
| HashText / ValidUTF8 / tokenizer | `package_compiler/Compile.chibi`, `source_snapshot/Snapshot.chibi`, and `source_tokenizer/Tokenizer.chibi` already read byte arrays once via strings.Bytes/text_bytes. This still allocates an int array, but avoids repeated source scans. HashText's old comment about ByteAt measuring text is not current runtime evidence. |
| M2 MagnitudeDivide | `7b6d4c42f6ac9ad25f64b1ba7283fc65676bb809` replaces shifted-array element mutation with MagnitudeAdd to preserve argument sharing. Separate array/compiler pattern, not a text-owner fix. Historical speed claims were not rerun here. |

## Validation and bounded follow-up

Same host/toolchain, pinned dependency archive and standalone flags as the
[first receipt](README.md#reproduction-and-bounded-validation), using the
fresh isolated `scratch-profile/text-build` directory. All consumers in this
driver were rebuilt; no instrumented library was installed or used by M2.
No authored/compiler asset generation was required. Configuration completed
without bootstrap work. Build: **6.973 s**, maximum child RSS **176,635,904 B**,
two jobs, 180-second subprocess timeout.

```sh
# Repeat the first receipt's standalone CMake command with
# -B scratch-profile/text-build, then:
cmake --build scratch-profile/text-build --target test_tools --parallel 2
python3 tools/test_policy.py --timeout 15 native /usr/bin/time -l \
  scratch-profile/text-build/test_tools --text-ownership-probes
python3 tools/test_policy.py --timeout 15 native /usr/bin/time -l \
  scratch-profile/text-build/test_tools --ownership-probes
python3 tools/test_policy.py --timeout 20 native /usr/bin/time -l \
  scratch-profile/text-build/test_tools --types
```

| Check | Result | Process elapsed | Maximum RSS |
| --- | --- | ---: | ---: |
| Text ownership | 2 passed / 1,416 assertions | 0.34 s (policy 0.444 s) | 1,835,008 B (1.75 MiB) |
| Prior collection ownership | 4 passed / 1,537 assertions | 0.00 s rounded | 2,031,616 B |
| Existing types | 27 passed / 4,861 assertions | 0.00 s rounded | 3,391,488 B |

Raw logs: [text](text-probes.txt), [collection regression](text-regression-collections.txt),
[type regression](text-regression-types.txt). Checkout path prefixes are replaced
and trailing whitespace trimmed. Independent source and receipt review found no
blocking lifetime or accounting issues. Touched C was formatted with version22;
`git diff --check` passed. The previous unchanged arena-cache assertion remains
an upstream issue; broad suites, sanitizers, authored VM/compiler execution and
full self-compilation were not run.

The narrowest justified next step is **call-site/lifetime attribution for one
small compiler string operation**, separating ordinary-arena descriptor/cleanup
creation from core-slice payload copies. The current probes establish mechanics,
not compiler dominance. Do not remove frozen-cache guards, release borrowed old
handles, or change GC policy. Existing Count/Split fixes should be retained;
any proposed descriptor reuse or slice-sharing change first needs proof of the
specific owner lifetime and retained-backing tradeoff on that bounded path.
