# Accepted runtime 308: focused Math validation, 2026-10-28

All eight requested selectors passed on the new runtime: 58 correctness cases / 327,220 assertions, plus the opt-in benchmark’s 1 case / 2,021 assertions. This is a fresh focused native proof after the arena C-layout change. All pre-308 results remain historical; they are not transferred to this runtime. Normal full Graph/asset/installed-SDK gates and Math publication remain held.

The immutable source was clean Math `b87afaea2106dd8f56ff0ededc6d4604151096e5`, tree `cfb02134ea9f59bbfe1023808ee2489439ae7aee`, containing accepted runtime `30837bd5dd057b93b9fa082d456ce9f3b9ec5bb7`. Its private snapshot honestly reports `b87afaea2-dirty`. Only the authorized optional asset-host condition in `test/language/CMakeLists.txt` changes to `if(FALSE AND TARGET Chibi::asset_compiler_host)`. Helper patch SHA-256: `69062a481869920fc8585c1531556f5dda7dee9d422abde4f9d3ee70f108e163`.

All 3,473 tracked files were compared before and after. Every other byte matched b87, including all new runtime/arena headers, selected tests and registration, Math source, compiler target code and checked bootstrap pair. No old objects/artifacts were substituted, no Body/801 or unaccepted sibling source was copied, and no production source, guard, warning or assertion setting changed. Previous snapshots and failures remain preserved.

The fresh standalone configuration used Clang 22.1.8, Release, compiler SDK ON, native/desktop SDK OFF, four jobs and no sanitizers. Selected test compile flags retained `-O3 -DNDEBUG -std=gnu23 -arch arm64 -Wall -Wextra -Werror -UNDEBUG`. Configuration took 5.369 s; building only `test_language` took 63.549 s. The build began with no driver or objects and performed 486 compile invocations, creating 486 objects with new timestamps. Parallel output interleaved one archive/compile line and split another compiler command with a progress message. The log contains 486 lines with ` -c `, including 485 complete recognizable clang compile lines; independent output/source pairing accounts for all 486 invocations and objects. Neither the dependency graph nor actual build invoked the optional asset-host consumer, asset/facade generation or complete host executables. No complete bootstrap/self-build ran.

Fresh driver SHA-256: `f5de82109feeda547055a7c14f247a5eb8521c6be31dc0b2f6dcfc3b7d64b7fa`. Fresh `compiler_target.c.o` SHA-256: `64bef407ce29f4e1f0e239c6f76909a7b7ca400afb6c6c8827339e8193bd9503`. Its source hash, complete compile command and 27 generated-header dependencies are retained; the arena dependency was checked against the new source header. Existing fixtures constructed and exercised dynamic target arguments using these fresh layouts. No standalone target JSON was captured or borrowed, and target-byte equality with an older runtime is not claimed. Prepared format remains 26; that version number alone is not a layout-compatibility proof.

| Existing selector | Passed cases/assertions | Outer test s |
| --- | ---: | ---: |
| `--package-math` | 6 / 4,132 | 1.115 |
| `--package-compiler-chibi-imports` | 2 / 17,453 | 32.343 |
| `--package-compiler-chibi-imports-compiled` | 2 / 17,024 | 24.135 |
| `--package-imports` | 18 / 1,530 | 0.558 |
| `--package-snapshot-chibi` | 16 / 286,093 | 0.381 |
| `--record-publication` | 7 / 230 | 0.176 |
| `--native-calls` | 7 / 758 | 0.267 |
| `--package-math-benchmark` | 1 / 2,021 | 0.470 |

Math ran 29 authored tests on BYTECODE and 29 on SLJIT. Both compiler modes passed the importing-package corpus, including nine reachability additions and both Math samples. Emitted Math calls require positive SLJIT entries, zero bytecode entries on compiled calls and zero inspection fallback. Imports likewise retain their exclusive-backend counter checks. The general compiler fixture keeps its existing size-based bytecode fallback exception and does not print numeric fallback counts; no blanket zero-fallback claim is made for the compiler itself. Record publication covers BYTECODE/SLJIT preparation and publication, including the accepted owned-default lifetime oracle, without claiming authored execution. Native calls also test the intentional inspected VM path.

Benchmark code and all six Math sources were verified byte-identical to historical 699. Both runs used the same 128 loop steps, 16 invocations per trial, three trials per backend/body and one warm-up invocation. The following comparison is observational across the whole runtime change. It does not assign cause to an individual #797 change or establish an expected baseline/budget.

| Backend/body | Median ns/step, 699 → b87 | xalloc calls/trial, 699 → b87 | Requested bytes/trial, 699 → b87 |
| --- | ---: | ---: | ---: |
| BYTECODE ImportedLerp | 599.121 → 632.324 | 0 → 0 | 0 → 0 |
| BYTECODE InlineLerp | 254.883 → 263.672 | 0 → 0 | 0 → 0 |
| BYTECODE SpringValue | 13,597.168 → 13,330.078 | 23,136 → 2,672 | 5,474,048 → 2,522,496 |
| BYTECODE SpringFields | 13,954.590 → 13,929.687 | 21,088 → 2,672 | 5,179,136 → 2,522,496 |
| SLJIT ImportedLerp | 103.027 → 97.168 | 0 → 0 | 0 → 0 |
| SLJIT InlineLerp | 5.371 → 5.371 | 0 → 0 | 0 → 0 |
| SLJIT SpringValue | 3,454.101 → 3,074.707 | 20,624 → 128 | 3,024,128 → 71,040 |
| SLJIT SpringFields | 3,421.387 → 3,075.684 | 18,576 → 128 | 2,729,216 → 71,040 |

Current spring arena-block counts were 112 per BYTECODE trial and 16 per SLJIT trial; lerps reported zero. Spring arena usage/capacity was 3,734/4,890 bytes for the record form and 2,702/4,890 for fields. Current activation bytes were 7,376/6,424 on BYTECODE and 63,760/63,624 on SLJIT for record/fields. These are separate sampled footprints, not a combined memory total. Counter scope is xalloc wrappers and arena blocks, not all process allocation. Invocation timing includes reusable storage reset and binding; the short timing samples are not a general throughput claim. Benchmark measurement does not print execution-entry counters; strict backend evidence comes from the correctness/import checks.

No red result occurred in this fresh route. Pre-308 normal asset/bootstrap and Wasm failures remain historical receipts; none was silently reclassified or cleared. This run supplies no new-runtime Wasm, full bootstrap, full asset producer, ordinary complete build or installed-SDK proof. Those remain with their assigned owners and the coordinator’s ordered gates. Scratch, binaries and all historical evidence remain retained.

[native-308.json](native-308.json) contains exact commands, source/object/driver hashes, current target-object/header/flag evidence, individual timings, backend scope and the matched historical benchmark comparison. Its compile-log counter labels incorporate the independent audit correction described above; the retained original summary is unchanged. [benchmark-308.json](benchmark-308.json) retains all 24 current trial rows and preparation/footprint counters. The helper patch, preflight/source inventories, dependency graph, object inventory and ten configure/build/test receipts remain in `.chibi/integration/native-math-308-evidence` in the Math preparation worktree. The authoritative implementation stayed unchanged during these runs.

## Separate clean Wasm checkpoint

The same clean `b87afaea2106dd8f56ff0ededc6d4604151096e5` source was built from an empty Wasm directory with normal strict flags and the six Math source embeds. Compilation took 112.585 seconds and produced 486 compiled objects. Math passed 6 cases / 4,132 assertions, with 29 authored tests on each of BYTECODE and WASM; packed row arrays passed 20 / 17,853 and package snapshots passed 16 / 286,093. These three selections total 42 cases / 308,078 assertions. The [Wasm receipt](wasm-308.json) records commands, clean before/after state and log hashes. The Wasm binary SHA-256 is `01598e12cb00891db7a75a747a92a9083764dc55645095c51b893cbbd56e3f34`. This separate check does not execute the compiled-Wasm compiler lane, full self-build, normal asset/Graph producer or installed SDK.

## Source boundary

Pure `4751f2aafb2e3352fe0e0c67f98ea7c8a6fad1d3` merges accepted `30837bd5` while preserving the reviewed eight-file reachability diff byte-for-byte. Math `b87afaea` merges that prerequisite and preserves every Math implementation and evidence path from `8d99bdb0`. All ten accepted runtime blobs match main exactly. Earlier results remain attached to their earlier runtimes and layouts. No unaccepted Body/capability source or authoritative compiler artifact was overlaid. Final compiler/SDK integration and the Math SDK publication hold remain open.
