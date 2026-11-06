# Recursive compiled call frames

The shared IR now has an explicit `BF_INVOKE` instruction. It saves the caller's
live values and returns a private call request to an iterative compiled-call
runner. The runner enters the callee's SLJIT code or portable instructions, then
resumes the caller after Return. Generated C emits the same request and uses the
same frame contract. Recursive execution does not grow the C call stack or enter
`eval_node` or the graph flow evaluator.

Each invocation has distinct argument/result banks, temporary value storage,
host bindings, continuation state and step budget. Frames keep stable addresses
when the stack grows. Caller values survive child calls; arguments and results
retain strings and nominal names. Suspended frames retain their current function
and context for inspection. Native and bytecode can alternate at waits using the
same saved values. Disposal walks retained frames iteratively.

The program owner must retain every referenced function generation until the
root activation and its children are released. Callee references are borrowed;
the call runner does not compile graphs or own programs. Pending external host
requests must be cancelled before their invocation storage is released.

## Current boundary

The graph compiler now lowers authored effectful recursion into immutable
function definitions. Acyclic calls still expand into static frames; a recursive
edge emits `BF_INVOKE` and references a separately compiled definition. The root
owns the definition group and references within that group are borrowed, so
mutual recursion has finite code and no ownership cycle. Definitions are queued
before their bodies are lowered. Generated C emits every body in the group.

Direct compiler tests cover self recursion, mutual recursion, outer callers and
linked libraries after the source library has been freed. Loops can remain active
across recursive calls and waits, with independent state per invocation. Pure
recursion and re-entry of the same active loop node within one invocation remain
unfinished.

The runtime inspection adapter now maps each dynamic invocation and its static
child frames into logical frame IDs. Call entry and exit hooks associate source
graphs, host requests, staged argument watches and stepping state with those
frames. Completed views snapshot their inputs and recorded values before the
callee storage is released. Externally retained host requests prevent reuse of
their logical frames. Ordinary completed frames can be reused by later calls.

Publication now accepts these definition groups. Recursive behavior dispatch
uses the compiled runner through the existing activation, host-wait and debugger
APIs. Tests compare source stacks, active leaves, errors, watches, traversal
records, stepping and breakpoint stops against the explicit oracle. Cancellation
preserves the root wake time rather than exposing the last child's wake time.
The shared runtime smoke exercises pooled reuse, host cancellation, paused host
completion and replacement of an idle instance's program generation. Replacing a
generation while recursive activations remain active still needs acceptance.
Suspended dynamic stacks are not supported by the separate activation-copy API.

`--recursive-function-audit` constructs a behavior event calling a recursive Sum
graph. It checks that oracle and default compiled dispatch both store 28 for
input 7, with no oracle calls during compiled execution. It also requires direct
lowering and actual whole-function dispatch. The audit now passes. This closes
the publication restriction for the covered effectful recursive calls; it does
not establish full language or standalone export coverage.

## Checks and performance scope

The IR fixture implements `sum(n) = n + sum(n - 1)` with a zero base case and an
owned string carried through calls and returns. Independent expected sums check
self and mutual recursion. Tests cover live caller values, depth and per-frame
budget errors, retained error frames, cancellation at a wait, ordered wake
times, managed returns after input/frame destruction and backend switching.
The deep case uses 2,048 nested calls. Generated C uses the same fixtures with
all callee code entries replaced, so a generated root cannot hide native callee
execution. The runtime snapshot matrix also replaces every body and disables
native dispatch entries before comparing generated C. The authored matrix checks oracle results, wake times, logical depth,
step counts and failure source nodes/pins across local and linked self/mutual
calls. Strings, nullable nested structs, arrays and sets survive destruction of
invocation storage and replacement of input buffers. The shared backend smoke
also runs authored and IR recursion in actual WASM.

Runtime snapshots now cover all 64 local/library, self/mutual, outer-call,
waiting, managed-value and active-loop combinations. They compare all four execution modes
through the same inspection APIs, including late trace activation, breakpoints,
stepping, budget/depth errors and completed child views. Nullable inputs and
missing Return are covered separately. These tests exposed borrowed scalar
watch type names surviving a freed recursive frame. Watches now retain those
names and reuse their allocation while the name remains unchanged.

The active-loop cases iterate twice, recurse only on the first iteration and
return after the loop completes. They check live caller loop state without
turning the fixture into exponential work. The runtime host matrix also includes
these loops across recursive host waits. This exercises function recursion;
same-invocation loop re-entry remains a separate compiler exclusion.

The linked fixtures exposed a serialization defect: Set defaults were written
as scalar values even though loading expected a sequence. Set defaults now use
the existing array representation while retaining Set metadata. The fixtures
check empty and populated defaults through library snapshot serialization.

Native `make test` passes with address and undefined-behavior sanitizers,
including Scene/editor checks, the authored catalog audit and application
headless smoke. The generated-C recursive matrix passes. `make web` builds the
application and executes the recursive smoke as BYTECODE with SLJIT absent.
The native no-JIT core/runtime smoke still reports UNAVAILABLE without an
interpreter fallback. Targeted formatting and `git diff --check` pass.

Additional native/bytecode error tests cover an unretainable argument and a callee that ends
without Return. Both retain the caller's previous result and identify the call
site. A completed child is released before exposing a return failure at its
parent; child execution errors instead retain the failed callee for inspection.

No authored Arena graph emits this instruction yet. This change does not claim
an Arena performance improvement or near-handwritten-C recursion performance.
The general resumable path currently allocates frame storage as calls enter;
frame reuse and cheaper synchronous calls still need measurement and optimization.
Acyclic authored calls retain their existing expansion and Tick batching.

The dynamic runtime integration passes the full native suite, generated-C
runtime snapshot matrix, recursive dispatch audit, web BYTECODE smoke and native
no-JIT rejection checks. The web build also exposed and fixed an omitted
`assert.h` include in the new frame adapter.

[Five paired Arena runs](dynamic-recursion-scene-results.json) leave native
behavior p50/p95 at 0.008/0.022 ms and simulation p50/p95 at 0.013/0.027 ms.
Portable behavior p50 stays at 0.018 ms; p95 changes from 0.031 to 0.032 ms.
This runtime coverage change does not establish an Arena speedup.

[Three scale pairs](dynamic-recursion-scale-results.json), 256 frames each,
produce these median times. The fixture holds positions fixed and excludes
physics and rendering.

| Entities | Native before | Native after | Bytecode before | Bytecode after |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.005 | 0.005 | 0.048 | 0.048 |
| 1,000 | 0.052 | 0.053 | 0.463 | 0.471 |
| 10,000 | 0.570 | 0.577 | 4.901 | 4.869 |
| 20,000 | 2.383 | 2.484 | 12.304 | 12.512 |

Native p95 worsened at both larger counts, prompting
[five longer native pairs](dynamic-recursion-scale-repeat-results.json) with
512 frames. At 10,000, p50 is 0.573/0.581 ms and p95 is 0.619/1.013 ms. Median
paired changes are +0.006/+0.327 ms; p95 worsens in four of five pairs. This is
an unresolved tail-latency regression signal, not evidence of unchanged scale
performance. At 20,000, p50 is 2.622/2.387 ms and p95 is 4.328/3.653 ms; median
paired changes are -0.180/-0.412 ms. The different direction at 20,000 does not
clear the 10,000 concern. No code changed between the two measurements. Both
reports retain all samples, memory, checksums and binary/content hashes.

After adding authored lowering, [five new paired Arena runs](recursive-lowering-scene-results.json)
again leave native behavior p50/p95 at 0.008/0.022 ms. Native simulation p50 is
0.013 ms in both binaries, with p95 0.027/0.028 ms. Portable behavior p50/p95 is
0.018/0.032 ms in both. This confirms no measured Arena behavior improvement
from the coverage change. The saved before binary matches the after hash in the
following IR-stage report; these are separate comparisons.

[Three paired scale trials](recursive-lowering-scale-results.json), 256 frames
per run, give the following median Tick times in milliseconds. These are fixed
positions with no physics or rendering.

| Entities | Native before | Native after | Bytecode before | Bytecode after |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.005 | 0.005 | 0.049 | 0.047 |
| 1,000 | 0.052 | 0.053 | 0.458 | 0.470 |
| 10,000 | 0.600 | 0.584 | 4.963 | 4.726 |
| 20,000 | 2.346 | 2.474 | 12.412 | 12.356 |

The native 20,000 result prompted [five longer pairs](recursive-lowering-scale-repeat-results.json)
with 512 frames each. Separate medians are 2.415/2.519 ms at p50 and
3.932/4.069 ms at p95, while median paired changes are -0.014 ms and -0.351 ms.
Individual paired p50 changes range from -0.167 to +0.222 ms. The inconsistent
direction and spread do not establish a reliable performance change. No code
was changed between these two scale measurements. The reports retain samples,
memory and content/binary hashes; the initial slower result is retained.

[Five paired Arena runs](recursive-call-scene-results.json) leave native behavior
p50/p95 at 0.008/0.022 ms. Simulation p50 stays at 0.013 ms, with p95 changing
from 0.027 to 0.028 ms. This provides no evidence of an Arena gain.

[Three paired scale trials](recursive-call-scale-results.json), 256 sampled
frames at each count, give native p50 before/after of 0.005/0.005 ms at 100,
0.052/0.052 ms at 1,000, 0.571/0.567 ms at 10,000 and 2.530/2.385 ms at 20,000.
The 10,000 native p95 changed from 0.609 to 0.744 ms, prompting a longer repeat.

[Five longer pairs](recursive-call-scale-repeat-results.json), 512 frames each,
give native p50 of 0.581/0.577 ms at 10,000 and 2.361/2.380 ms at 20,000.
Median paired p50 changes are -0.003 and -0.016 ms respectively. Native p95 is
noisy: separate medians are 0.898/0.950 ms and 3.329/3.922 ms. The paired p95
change at 10,000 is -0.058 ms; at 20,000 it is +0.250 ms. Native tail variance
remains unresolved, so these results do not establish a tail-latency improvement
or a blanket absence of regressions.

The portable repeat gives p50 5.048/4.733 ms at 10,000 and 12.346/12.172 ms at
20,000. These runs use fixed positions and omit physics and rendering. They
check existing execution against the saved Set-stage binary, whose hash matches
the preceding report; they neither benchmark recursive graph performance nor
prove a playable large Scene. The scale reports preserve individual pairs,
memory and binary/content hashes.

```sh
build/test_suite --whole-recursion
build/test_suite --emit-recursion-c /tmp/recursive.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -I src \
  /tmp/recursive.c -o /tmp/recursive.so
build/test_suite --whole-recursion /tmp/recursive.so
build/test_suite --recursive-function-audit
```
