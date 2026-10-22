# Gas-grid activation IDs: accepted specialization, rejected alternatives

> Historical record: these measurements and plans describe a retired asset format.
> Current authoring uses ordinary compiled Chibi modules.

**Decision:** retain the specialized gas deposition change at `4490156b97d7345f676842d981f202cecd93b1c1`, now verified on `origin/main`. The change reuses integer IDs already produced while seeding each particle's 5×5 gas-grid halo. Bilinear deposition reads those IDs rather than repeating hash lookups for the same stencil cells. The source change is limited to `src/runtime/material_flow.c` and its tests in `test/runtime/materials.c`.

This is a scoped native gas-flow kernel result. It does not establish a general fluid-index win, a whole-frame or tail-latency improvement, or a GPU/web speedup. The separate unused-index skip and generic-map candidates remain rejected as defaults.

## Accepted activation-ID change

The matched native Release comparison uses baseline `897dd6deff962a5b97161c7cb02ccf078b4610fd` and candidate `4490156b97d7345f676842d981f202cecd93b1c1`. It contains six headless scenes, three paired trials per scene, 36 profile runs and 36 independently checked quiet-boundary hooks. The candidate improves median gas-flow time in gas-only and fog/liquid workloads, with all three paired trials improving in each workload. It does not improve frame tails consistently.

All percentages below use **positive = candidate faster**. Frame mean is the reported `runtime/total_ms` mean; paired values are reductions from corresponding baseline/candidate trial measurements.

| Scene | Frame p50 ms, baseline → candidate | p50 change | Frame p95 ms, baseline → candidate | p95 change | Runtime mean ms, baseline → candidate | Mean change |
|---|---:|---:|---:|---:|---:|---:|
| gas only | 0.339 → 0.333 | +1.77% | 0.395 → 0.413 | −4.56% | 0.338642 → 0.336321 | +0.69% |
| liquid only | 1.106 → 1.072 | +3.07% | 1.396 → 1.415 | −1.36% | 1.129262 → 1.107583 | +1.92% |
| fog + liquid | 0.987 → 0.961 | +2.63% | 1.125 → 1.077 | +4.27% | 1.000867 → 0.974008 | +2.68% |
| terrain barrage | 0.425 → 0.417 | +1.88% | 0.650 → 0.605 | +6.92% | 0.414242 → 0.401892 | +2.98% |
| enemy barrage | 0.474 → 0.477 | −0.63% | 0.591 → 0.597 | −1.02% | 0.429142 → 0.432825 | −0.86% |
| enemy collisions | 13.403 → 13.150 | +1.89% | 15.454 → 15.489 | −0.23% | 13.526871 → 12.995163 | +3.93% |

| Scene | Paired frame p50 reductions | Paired gas-flow reductions | Median gas-flow reduction |
|---|---|---|---:|
| gas only | +2.64%, −1.47%, +1.77% | +9.63%, +2.52%, +6.92% | +6.92% |
| liquid only | +0.65%, −1.42%, +4.70% | +17.02%, +10.28%, +30.11% | +16.96% |
| fog + liquid | +2.63%, +1.66%, +6.97% | +7.78%, +13.42%, +11.34% | +8.22% |
| terrain barrage | −2.79%, +1.88%, −0.75% | −2.70%, +3.49%, −0.10% | +3.49% |
| enemy barrage | −0.63%, +3.52%, −3.87% | −0.17%, +0.73%, −5.69% | −1.89% |
| enemy collisions | −2.33%, +2.24%, +1.29% | +1.23%, +3.33%, +0.36% | +1.83% |

The gas-only gas-flow median reduction is **6.921%**; its three pairs all improve. Fog/liquid gas-flow median reduction is **8.216%**; its three pairs all improve. In the gas-only frame measurements, p50 improves 1.77% overall, but only two of three paired p50 trials improve and p95 is 4.56% slower. These data support the kernel change, not a tail-latency claim.

## Rejected unused-index skips

The separate skip experiment compares `ba5383846be07b38ec936c0f51042661ad787c96` with `ce5e49e99a22619c64dd5100676ffead5480ec16`. It measures a targeted liquid single-fluid-tick proof: 60 warm-up and 240 measured frames in each profile, three paired trials per case, 18 profiles and 18 quiet-boundary checks. These fixtures disable the gas-cell path and keep 812 active liquid particles at their sampled boundaries. Replay hashes matched and behavior failures were zero. Positive percentages again mean faster.

| Targeted case | Index builds, baseline → candidate | p50 ms, baseline → candidate | p50 change; three pairs | Frame mean ms, baseline → candidate | Mean change; three pairs |
|---|---:|---:|---|---:|---|
| reactions disabled, no obstacle | 1200 → 480 (5 → 2) | 1.103 → 1.074 | +2.63%; +3.08%, +2.45%, −2.48% | 0.977613 → 0.973817 | +0.39%; +2.97%, −0.18%, −1.94% |
| reactions disabled, far obstacle | 1200 → 960 (5 → 4) | 1.067 → 1.129 | **−5.81%; −6.37%, −3.68%, −3.29%** | 0.990825 → 1.003150 | **−1.24%; −1.24%, −3.04%, −2.26%** |
| reactions enabled, no obstacle | 1200 → 720 (5 → 3) | 1.117 → 1.088 | +2.60%; +0.91%, +3.13%, +0.83% | 1.027208 → 0.987575 | +3.86%; −0.11%, +4.80%, +2.36% |

The far-obstacle case regresses in every paired p50 and frame-mean trial, despite one fewer index build per tick. The optimization therefore remains rejected as a default. The earlier six-scene matrix did not activate these skip cases: build counts stayed at 1200 or 720 for both sources. The targeted proof is the relevant evidence for this decision.

The first retained diagnostic attempt stopped at baseline replay (`targeted/`, zero measured calls); retry 1 stopped with an assertion (`targeted-retry1/`, six calls). Retry 2 is the completed, independently audited data set. These failed attempts remain preserved and are not used as timing results.

## Separate rejected generic map

The one-pass shared-map experiment is an older, independent source pair: `e0c766b41d168908ec9716ccfec2a072a1395915` → `a6003cb5dc93654cc96a89389eafe20ac26455de`. Its gas-only p50 changed from 0.349 to 0.451 ms (**29.23% slower**); all three paired trials regressed by 24.45%, 29.23% and 24.57%. That result rejects the generic map for this workload. It does not describe the accepted activation-ID reuse change, which does not add that map.

## Correctness and limits

The accepted candidate passed native `make test`, the native, scalar and worker material suites, and eight same-candidate native/scalar/four-worker parity cases. The independent correctness audit checked eight cases, 66 responses, 530 migrated asset entries, 2,097 source fingerprints and 11 artifacts. These checks establish the audited behavior and parity for those cases; they do not establish bitwise trajectories against the old implementation. Short-profile results establish cross-platform hashes and counters, without separate short endpoint replays. The current baseline and candidate use migrated the former document format assets; migration provenance is retained separately from these same-version checks.

An independent native ASan/UBSan coordinate oracle also checked pre-pressure deposition and mass/momentum against the original accumulation order. The tested gas source and support bytes match the accepted slice; pressure and full trajectories remain covered only by the stated scene checks.

The native timing is headless and workload-specific. It does not cover rendered-frame timing, GPU execution, browser/Wasm speed, or comparison against LuaJIT, V8 or JavaScriptCore; those engine comparisons remain postponed.

The v1 native-audit adapter emitted a diagnostic `KeyError` for a snapshot entity with no optional name. The corrected v2 auditor completed over the retained timing data; this was an audit-tool correction, not a new timing run.

## Frozen evidence

The files below are retained under `/Users/cwaldner/Documents/Codex/2026-09-30/is-x20/outputs`. SHA-256 values bind the reports to the exact artifacts.

| Evidence | Retained path | SHA-256 |
|---|---|---|
| Coordinator decision | `gas-ids-main-20261001/coordinator-decision.json` | `8daea7402f09f2c3a88df8c08927d52480793b291b4d08550a08c10eec117b80` |
| Native timing audit | `gas-ids-main-20261001/independent-native-audit.json` | `b2600148d91cf0ad68614a304136778c71b9d02623d1f123d8cc30956920f201` |
| Raw timing summary | `gas-ids-main-20261001/timing/summary.json` | `0092db227613b83d5031e3d8ae810d9755e4b33d69b591bb3ca99e7274e02a7e` |
| Timing stdout / stderr | `gas-ids-main-20261001/timing.stdout.log` / `timing.stderr.log` | `111d5c6b2a8beb24398a4b58f7069f6317e518af75a08c3437f8e931cb295572` / empty (`e3b0c442…b855`) |
| Baseline native manifest | `gas-ids-main-20261001/baseline-native/manifest.json` | `003193d74f8061055ffb1abdce9c0c9ba8db39b11e1ab05800362460f6b1d00b` |
| Candidate manifest | `gas-ids-main-20261001/candidate-all/manifest.json` | `94a224b81ece6bb8eb8afd5128dd591f6c0625055e17f6e379a58811418dc9f4` |
| Build checks | `gas-ids-main-20261001/build-checks.json` | `b04c6ae2e0b22bc819803d8cff79e7c15d0193562689e21cf37b5bf634f5ed9f` |
| Full native test log | `gas-ids-main-20261001/native-make-test.log` | `cb7075b78e1cafced6c64f5a6bd51b6cc26f6193b775e7372b8299ed255a8eda` |
| Candidate native material / Release logs | `candidate-native-material.log` / `candidate-native-release.log` | `c21f6720666b59ac1a8be903be0bb604393b8fc12eaf38b7fc20058b0af38b14` / `defc11d1063e7857870629abb99a223d41045371c23c32e532826658da2fe47d` |
| Candidate scalar / worker material logs | `candidate-scalar-material.log` / `candidate-worker-material.log` | `aa31f37423015fcaf9ecb97d4cbda8eb27e3a6a43c384070e8e605dfa2f5fc3a` / `34d3ceee1dcf08480d9a4f7616402e5fef115376199f486614c3ddb011b353d8` |
| Correctness audit / parity summary | `gas-ids-main-20261001/independent-correctness-audit.json` / `parity-4490156b/summary.json` | `d0a99a3a23456fca7a003d361d2d222345a242f669833b0ba84ae42927d749c5` / `407051e17419bb29c05cbf50e4742413b8ea9815be03de8b7961cd84b30d5d69` |
| Retry 2 skip timing / audit | `fluid-index-skips-20261001/targeted-retry2/timing/summary.json` / `independent-audit.json` | `65d68060106abf88aeeb645b463463acb6e6aa724776078abc97da3453aeac2f` / `9575396e4a4b7c4b23c9dec715d4771a8a936d02101325cf74c84665f1b92b6c` |
| Retry 2 inputs / authoring fingerprints / gate | `fluid-index-skips-20261001/targeted-retry2/inputs.json` / `authoring-fingerprints.json` / `gate-status.json` | `61b0be874f0f05379aedea2ce1d1bde46765ae044d99edd022454de16cf106c2` / `cfdc6ccc0cdefb75691b0eb542365500bc394561ddb98ce22b1b7a45c268eddc` / `778ee0b0f7034b1825ec953eb1c767e3bf930d9951a4d4f04dd995a33e6f1e66` |
| Retry 2 source manifests recorded by fingerprints | baseline / candidate | `b3e40efe5b114f3893c8f38ca84732b6ef2f90a111bb7e2da52cb30a450cd0ba` / `6921789e087cd1739603675624bb88943a2e4af81774db1a2b5c7587cc283909` |
| Earlier targeted gate records | `targeted/gate-status.json` / `targeted-retry1/gate-status.json` | `06b52956fff2e6d5ff10848e2c1f98a762238774cacf2e4c59783f917875e009` / `dd11076887f79095ed39e5364330059636bffbac6e898a074afe99fbfd8f4a6d` |
| Corrected auditor source / v1 diagnostic | `gas-ids-main-20261001/audit_native_v2.py` / `independent-native-audit-v1-diagnostic.json` | `5368f657c1b0f72883858f264fa8dc8e85aed37c4848126973a558872cd5971a` / `21e0e77b5a47b06ae5be3af3a44d24086967dead075d7f745de46e526314ae9e` |
| Generic-map one-pass audit | `fluid-spatial-repair/native-timing-onepass-independent-audit.json` | `83e388c7e2743ebe4d447c9619f19bff27374b282516f1cd814b5b7c01f87f90` |

The current-main coordinator excluded `ea03d0aa`, `a6003cb5`, `ce5e49e9` and `562fac8d`; the generic map and unused-index skip experiments are not part of the accepted source.
