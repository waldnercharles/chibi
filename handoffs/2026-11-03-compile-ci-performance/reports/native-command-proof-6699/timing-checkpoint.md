# Native command proof and compiler-cost checkpoint

2026-11-03. Local observations use the authorized Mac. They are shared-load correctness/cost diagnosis, not controlled before/after results or a complete-CI improvement.

## Delivered changes

- `4e5539676fb9d085b7bec26237114a45df9f9254` makes the independent exact Driver streams and complete selected Graph compile/link fixtures mandatory in native self-host. Historical VM comparison remains opt-in. Coordinator integrated it as `d3a829bec70584e0833600d1c45968ea6a113c06`; all five files match the reviewed bytes. Real raw fixed point, Windows cross output and actual contract passed, followed by diagnostics/test attributes. Source/tool guards held.
- `bc6d971d33be36a8d7f8dd32b86253d9fe7f46e5` reuses successful Release chibi through the existing explicit compiler option in later Linux core commands. Source prediction: successful-cold core old-seed whole-compiler constructions fall from three to one. Required outputs stay three; initial bootstrap, Debug/C/sanitizer options, selector inventory and independent seed-started proof stay. Independent review, all11 focused regressions, all51 infrastructure tests and a real current-compiler Graph selector pass. No measured CI speedup yet.
- Temporary branch-only producer `36506138dab6e97ccd9ebc99cd36e8b5eb96dcb3`, tree `e389beee99626474142328773f28364ca5859ca8`, was reviewed, pushed and dispatched as [run41627603178](https://github.com/waldnercharles/chibi/actions/runs/41627603178). It uses the unchanged published c5 seed. It is a single-purpose Linux artifact producer, not the complete CI matrix or a workflow intended for normal-CI merge. The production one-workflow assertion remains unchanged.

## Measured work

| Frozen workload | Wall | CPU | Meaning |
| --- | ---: | ---: | --- |
| Exact4e native self-host, Windows cross build and real command contract | 73.59s | 308.97s | Sequential coarse stages21/22/20s; stages2/3 physically equal, SHA b633bdae… |
| Same4e producer diagnostics, original aggregate | 49.47s | 83.94s | Initially unexplained aggregate; passed |
| Frozen diagnostic probe construction | 51.703s | 90.12s | 99.434% of measured build+execute wall |
| Execute its unchanged23 scenarios | 0.294s | Below displayed precision | Assertions and private process behavior preserved |
| Same frozen frontend/IR through existing emit-only branch | 11.100s | 10.94s | 81,611,164 bytes,55 pieces; also joins bundle and skips tool validation, so not an exact normal-build prefix |
| One retained4.68MB piece: O2 optimizer | 3.544s | 3.51s | SROA0.660 CPU; parsing0.0716 CPU, module-specific diagnostic profile |
| Same piece: object generation | 4.386s | 4.35s | Greedy allocation1.507 CPU, coalescing0.913 CPU; parsing0.0319 CPU |
| Exactbc6 real existing Graph selector through current compiler | 35.46s | 130.92s | Includes current compiler/entry preparation and actual contract, no C fallback |

CPU is summed process CPU, not total CI runner work. The table mixes distinct qualified workloads deliberately and must not be summed into a whole-CI latency. One module does not establish the critical path of the55-piece jobs=2 pipeline.

## What the code investigation found

The diagnostic program assembles39,312 bytes but its static import closure is98 files/3.42MB. Its23 in-process scenarios are cheap; compilation is expensive. The retained text has341,749 allocas,486,758 loads and422,376 stores. `scalar_llvm.Lower` alone has16,901 allocas. Its existing IR validation already proves unique slot definitions and producer dominance, yet all scalar outputs are stored then loaded.

All732 repeated definition groups have identical bodies, adding3.72MB across pieces. The largest are live internal helpers intentionally emitted per piece. Conservatively unused helpers account for only240.6KB/0.295% of text; no material saving is predicted from that cleanup. Sharing live helpers changes linkage/optimizer visibility and has not been proposed as a patch.

The next bounded backend experiment is a primitive literal directly feeding an adjacent comparison. The retained bundle has9,076 qualified-looking shapes. Re-establishing eligibility in semantic IR could remove one alloca/store/load per accepted case, while retaining labels, checks, comparison-result storage, addressed values, ownership/GC roots and floating encodings. This is a proposal, not integrated code or a performance result. It needs no new SSA/CFG framework; validation already owns the relevant facts.

## Outstanding evidence

The actual published-c5 Linux admission reached its unchanged90-minute outer deadline with status124 in the provided local Linux image under Rosetta, network disabled and jobs2. Controller construction passed8.394s; full compiler construction produced no module, object or executable file. All3,485 source files and seed bytes remained unchanged, and only the owned container was removed. This is incomplete admission, not unsupported-source proof or a hosted timing prediction. See `producer-draft/c5-linux-admission-terminal.json`.

At84 minutes, own-container accounting showed about5,045 CPU seconds, almost entirely user time, with only9ms quota throttling. The compiler's observed VmHWM was1,231,080KiB; container memory peak to that observation was1,292,836,864 bytes, including processes and page cache. These are incomplete-run observations. The original driver writes module files before starting opt/llc, so the empty output directory locates this delay before the external object pipeline; the exact earlier subphase/function remains unknown. The coordinator authorized the real hosted producer in parallel with all acceptance guards retained.

Only a completed run with the exact successful Linux job and unique unexpired matching-source artifact can supply the Windows consumer. The actual extracted PE hash still needs to be recorded. No Windows execution, seed publication, pin mutation or complete exact-final-head CI is claimed here.

Controlled compiler-rebuild/test/edit comparisons remain held pending an agreed quiet window. Existing earlier controlled results retain their original frozen revisions in `compiler-comparison/comparison.md`; they are not measurements of these newer integrated sources. Full CI wall latency and summed runner work remain separate unproved deliverables.

Evidence directories: `real-proof/`, `canonical-candidate-reuse-audit/`, `diagnostic-cost-audit/`, `producer-draft/`, and sibling `producer-41627603178/`.
