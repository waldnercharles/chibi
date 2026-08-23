# Phase 10 scene acceptance measurements

Measured 2026-08-23 from the Phase 10 runtime slice with:

```sh
cmake --build build
./build/scene_benchmark
```

The harness seeds the same scene twice with seed `0x12345678`, advances 600
frames, and compares each frame hash. One run pumps attachments in registration
order; the other uses the host parallel callback in reverse order.

```text
scene_600_frames_identical=1
scene_final_hash=18192870856481213669
```

The budget run creates 2,000 live entities and attaches the per-frame behavior
to 500 of them. It reports behavior time separately from systems and rendering
through the same timing path used by `ENG-7`.

```text
budget_entities=2000
budget_behaviors=500
budget_average_behavior_ms=0.564400
budget_max_behavior_ms=0.926000
```

Both measurements are headless. The editor-only authoring walkthrough remains a
window check in `docs/manual-verification.md`.
