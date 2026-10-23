# Rich compiler v3: independently reproduced load rejection

This is a bounded Linux x64 diagnostic, not acceptance of the compiler WIP, a new runtime limit, or a dependency of the scalar native producer.

## Exact inputs

- Source WIP: `aec822eb5d98cbdf987f81766e33cf02e2b33308`, tree `1a59410022d128108e7744cf2b5641047a44d713`.
- Evidence handoff: `0b1492796941d6bd4159dc8b920f7ca8fe35c2c9`. All 32 candidate source hashes and all 23 handoff evidence hashes verified before and after execution. The own checkout remains unchanged.
- Producer compiler: checked-in seed `41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e` (12,098,990 bytes), invoking `project:package_compiler:1:CompileClosure:callable` against the manifest's exact ordered 18-source closure and target. No recursive source overcollection.
- Unchanged pinned runtime: `0d0783e39e8547853c1310945ea4d508c05ae4e8174d4a976e499853b136be1d` (1,865,096 bytes), source `5c28191b3ea53afa232c34e37b14fd9b01024aef`; the candidate already pins this runtime. No identity overlay.
- Preserved first-stage output: `91261ee1584a09759e87f6c27f1b41a37063dc2291be57c1e8e33b0f6d8a7213` (13,743,964 bytes). `--output-bytes` preserves the result without claiming its validity.

## Normal runtime evidence

The existing `--bootstrap ... --recover ...` entry failed after 10.05 seconds with exit 1:

```
bootstrap: Prepared module project:package_compiler: Prepared shader decoded storage limit
```

No recovered artifact was published. The first-stage producer separately succeeded on both bytecode and SLJIT with 22,001,905 selected-backend entries each, zero fallback/overflow, and byte-identical output. Independent loads of the preserved output rejected before invocation on bytecode (0.149 seconds) and SLJIT (0.184 seconds), with the same module/error. No execution-counter claim is made for these failed loads.

The normal recovery process's sampled RSS maximum was 1,608,560 KiB and observed kernel HWM was 1,646,448 KiB. These are process measurements, **not** canonical storage, per-phase temporary storage, or proof of frontend destruction.

The runner owns logs before signal setup, tracks cancellation during child creation, establishes a process group, enforces an outer 180-second deadline, and terminates/reaps the owned group. PATH is empty and CC/CXX unavailable. It does not run Build or third-party tasks.

## Narrow diagnostic

The error originates in the generic per-wire owned reservation check. The word “shader” does not identify a shader producer.

An unchanged private compiler-host executable was rebuilt from exact d0de sources and the official d0de SDK archive `ec7179cb5b55456d112cb65134b9692e052866339526cedea7ebfcba0bc5087b`; both backend controls reproduce the same rejection. GCC 14 compiled existing sources only; no host shim/API was introduced. The SDK's static execution library is not PIC (TLS relocation), so an attempted shared library was not usable and no same-process ctypes proof is claimed.

Independently source-reviewed scratch patch `9f3748c78b13fffd5893e6ddad49a2756303998730477037e84657619323d176` logs only after the existing owned-limit predicate has failed. Diagnostic binary: `870d3e509a0c31dcd1c3ed935fdce9df6a44579144a6198ed5b52380e1d9297e`.

The follow-on reviewed patch `2aa39febbb20780e85e60da4989d552b7441c54298bcd8bd3175b3df0bdc5c3c` adds only the original handoff's per-function preparation and aggregate preparation log lines. Diagnostic binary: `3d0c6c6215ebf731ac725568d4615d259e9f5d106259f0debc0bded500c79674`. No production source, predicate, limit, accounting update, or return path changed.

Both diagnostic backends give identical numeric lines:

| Quantity | Bytes/count |
|---|---:|
| Preparation request | 233,103,113 bytes |
| Prior wire-owned charge | 39,123,177 bytes |
| Wire-owned limit | 268,435,456 bytes |
| Shared charged storage | 45,107,417 bytes |
| Shared remaining storage | 209,006,334 bytes |
| Compiler functions | 368 |
| Service count | 5,622 |
| Module offset and size | 11,973,448 bytes |

Request plus prior owned charge exceeds the wire limit by **3,790,834 bytes**. The request also exceeds shared remaining storage by **24,096,779 bytes**, but that subsequent predicate is not reached. These are conservative preparation reservation amounts; they are not allocations observed to occur and are not an actual peak-memory measurement. Backtrace logging itself can allocate, so instrumented RSS is not production-memory evidence.

Top preparation contributors, bound to exact source byte spans in `preparation-summary.json`:

| Function | Reservation | Matrix component | Source span |
|---|---:|---:|---|
| Body.EmitBody | 39,974,045 | 27,100,152 | Body.chibi 87,106–135,078 |
| Steps.StatementStep | 23,343,248 | 14,286,888 | Steps.chibi 12,439–50,384 |
| Steps.CallStep | 22,016,930 | 13,269,472 | Steps.chibi 100,729–137,766 |
| Compile.Assemble | 12,160,654 | 6,008,544 | Compile.chibi 78,585–105,068 |
| Steps.ValueStep | 11,950,455 | 6,059,136 | Steps.chibi 50,450–73,083 |
| Steps.OperatorStep | 6,149,190 | 2,378,992 | Steps.chibi 137,830–151,144 |

Historical v1/v2 receipts use different source trees and a macOS host. Their command is build-host with compiler/bootstrap loading; they must not be casually described as native-produced artifacts. Different platform layouts, hosts, source trees and routes prevent attributing numeric differences solely to the v3 refactor. Historical context is recorded separately, not as a controlled comparison.

## Remaining lifetime gates

The scalar leaf producer is separate and does not need this rejected rich compiler. A producer process can return independently owned serialized data and exit before a backend process begins; this is not a same-process teardown proof. Existing raw-byte CLI mode frees input and host before writing output; existing JSON mode writes output before freeing the host. A stronger same-process test must explicitly free the frontend, retain only owned output, exercise allocator reuse, then load/invoke the backend and ultimately Driver.Object. Canonical module storage, observed temporary phase storage, process RSS, and preparation reservation bounds must remain separate.

No default, seed, ABI21/22, quota, mixed-base Project, permanent transport/API, or production runtime change was made. No full suite, non-Linux run, game compiler acceptance, or completed scalar lifetime gate is claimed. Generated SDK extraction, private diagnostic executables, preserved compiler closure, and evidence remain in this task's scratch directory pending review/cleanup.

## Evidence representation

`results.json` retains receipt hashes, measured outcomes and normalized commands; absolute local paths are replaced with placeholders and per-process samples remain in scratch. `preparation-summary.json` preserves every numeric diagnostic line and source hash. Diagnostic patches are inactive evidence, not production changes. `host-build.json` binds existing source and official SDK identities plus normalized compiler commands; GCC-only `-Wno-multichar` accepts the existing DYNA canary literal without disabling runtime assertions. No generated executable or compiler artifact is included here.
