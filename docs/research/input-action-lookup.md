# Input action lookup after editor integration

September 13, 2026. Integrating the authored action bindings exposed a population
scan outside the timed simulation step. Every input sample asked whether each
action had a listener. `behavior_has_event` walked every attachment and every
node in its graph; an absent action scanned the entire crowd. At 20,000 enemies,
a native profile attributed 2,428 of 8,005 main-thread samples to input sampling,
almost entirely this lookup. The benchmark's broad `render_host` bucket included
that work; it was not all renderer cost.

The behavior world now counts attachments per registered asset using the existing
attachment index updates. Event availability examines active asset generations
and their compiled event tables. It excludes registered assets without instances
and follows replacement, hot apply and the last detach. The work scales with
distinct assets and events, rather than enemy count or graph size. The native
SLJIT and portable bytecode runtimes share this host-side improvement.

The scale fixture also needed two content updates: count the authored Phase
Warden toward the requested population, and use the authored mouse-fire binding.
Both comparison binaries contain these fixture updates. They use the same Arena
assets, so the comparison isolates event lookup. The earlier September 13
performance samples used different Arena content and input bindings.

Three alternating paired trials, Release without sanitizers, 30 warmup and 240
combat frames, 20,000 initial enemies, 1280x720, Auto eight workers:

| Median of trial medians | Before lookup change | After |
| --- | ---: | ---: |
| Whole frame | 23.312 ms | 16.262 ms |
| Behavior/runtime orchestration | 4.851 ms | 5.427 ms |
| Physics | 7.024 ms | 6.973 ms |
| Rendering and host work | 9.568 ms | 1.917 ms |
| Other systems | 1.015 ms | 1.002 ms |
| Presentation | 0.062 ms | 0.061 ms |
| Frame p95 | 35.322 ms | 28.959 ms |

The frame median falls about 30%, equivalent to 43 versus 61.5 FPS. Individual
phase medians do not sum to the whole-frame median. This does not establish
locked 60 FPS: the p95 still exceeds the frame budget, and physics/contact work
and Tick schedule preparation remain the larger opportunities.

Every run retained eight behavior and rendering workers, zero interpreter calls,
the same position checksum, and the same frame-by-frame combat trace. Each ended
with 19,991 enemies, nine kills and seven experience, and passed restart checks.
[Raw paired results and executable hashes](input-action-lookup-results.json).

The existing attachment-index regression now checks event availability before
attachment, across multiple instances, after asset replacement and hot apply,
and after the last instance detaches, under oracle, SLJIT and bytecode execution.
CI additionally runs a 2,048-enemy combat smoke with parallel execution required;
it checks population, firing, compiled execution and restart without imposing a
timing threshold on shared runners.
