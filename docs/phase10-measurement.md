# Phase 10 slice 0 measurement

Measured 2026-08-23 at commit `99a99f1` with:

```sh
cmake --build build
./build/bench_activation 100 100
```

The headless harness dispatches `Tick` to 100 persistent Blueprint instances
for 100 frames. The graph contains eight nodes and takes the true Branch path.
Each completed activation is released before the next frame. The pooled variant
reuses activation storage and dense per-node runtime storage after its warm-up
activation for each instance.

```text
variant=unpooled_baseline
activations=10000
ns_per_activation=2821.300

variant=pooled_dense_store
activations=10000
ns_per_activation=2106.500
```

The baseline is the current per-activation allocation path. The pooled result
is 25.3% lower in this run. No `ecs/`, `engine/`, or `game/` code is included
in the benchmark or was started before this report.
