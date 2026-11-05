# Complete Linux Build and Driver validation

The complete authored `BuildTests.Run` and `DriverTests.Run` passed on Linux x86-64 and were independently accepted. Both used source tree `9432a0d8292216b4221d24864b1b9b40a543b325`, published as `bd31dc875805c64ebdb1584345cd321a584b693d`. The local tested commit was `a8d16f54ce664bbc077558f01f16365f1c7f899d`; publication changed commit metadata only. Later Discovery fixture changes are a separate validation gate.

## Build

All 30 authored assertion sites remain. The complete entry constructed real native Graph programs and exercised its 11 Graph calls, real C compilation and archives, header staging, cold/warm builds, source/header/deleted-object/flag invalidation, failure/retry, archive consumer and headerless executable. It copied the authored fixture subtrees, mutated only that private clone and removed it on success. These are full-entry results, not a selection of successful assertions.

Main construction, Run, worker and outer process exited zero. The owned phase took 21.035 seconds; observed outer elapsed time was 21.120 seconds. The fixed 1,200-second total came from the registered Build ceiling and included construction, execution, final fences and its ten-second cleanup reserve.

## Driver

All 117 authored assertion sites remain. The complete entry built and ran the real native Driver, then exercised command planning, selector timeout, overlap/exclusive scheduling, fixed-point planning modes, controlled comparison-seed requests and Git census/cleanup. Its compiler/stage planners use controlled shell fixtures; those modes are not new real compiler bootstrap or fixed-point evidence. The comparison fixture did not download a seed.

Main construction, Run, worker and outer process exited zero. The owned phase took 19.031 seconds; observed outer elapsed time was 19.117 seconds. The predeclared 240-second total included construction, execution and final fences, with ten seconds reserved for cleanup inside that total.

## Common guarantees and remaining gates

All four top-level split streams and each owner log were empty for both entries. Authored negative cases validate their internally captured streams; those inner captures were not separately retained. Independent review checked actual executable identities, complete source and tool closure, wait/adoption records, ECHILD, EOF and definitive post-ECHILD fences. No owner signals, errors or surviving children remained. Authored scratch was removed, normal build cache was retained, and private home/temp/cache/Git-template directories were empty. Driver's fixed-point parent directory remained empty as authored.

Each run revalidated 4,176 source entries, 578 pinned file facts, 34 directory inventories and 913 required absences. Both kept normal compiler job selection and affinity [0,1]. The original compiler and private copies returned to mode 0600. Assertion-site counts are source inventories, not dynamic assertion counts. These are bounded correctness timings, not benchmarks.

Exact artifact and receipt identities are in [graph-build-driver-results.json](graph-build-driver-results.json). Direct full-entry dispatch does not validate the `tools/build.sh` registry adapter. Discovery, other final-source Graph entries, actual Windows and broader current product/runtime/C integration remain separate. Whole-suite containment does not establish the stronger historical per-Capture descendant-drain proof. PR #1331's explicit hold, the unknown original c5 bootstrap and excluded native union leak investigation remain unchanged. No merge or release is implied.

## Local preservation

The verified archive `chibi-graph-build-driver-a8d16f54-source-proof-20261105.tar.gz` is 23,276,108 bytes with 12,026 members, SHA256 `663400a72d659f3bb9629b19c4f88fbf99054924405b9d871850dd5d6264f76c`. Its 28,765-entry inventory includes 16,741 explicit byte-and-mode-identical duplicate mappings. Every archive member, both source trees, complete packet inventories and pinned inputs were reverified. Large dependencies remain external and pinned. Packaging performed no native execution or upload.
