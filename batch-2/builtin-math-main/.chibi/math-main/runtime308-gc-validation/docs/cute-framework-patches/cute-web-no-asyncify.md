# Run Chibi's browser frames without Asyncify

`cute-web-no-asyncify.patch` removes Cute's Emscripten Asyncify link flags and its forced `-O1` compile option.

## Why retain it

Upstream commit `96e6034aa6bc44665856ee480962a9ddab0b87cc` still links the web build with `-sASYNCIFY`. Chibi schedules browser frames through `emscripten_set_main_loop` in `src/host/player_main.c` and never suspends inside a frame, so it does not need Asyncify's instrumentation.

Upstream took the other half of the earlier patch in [ff7f2a58](https://github.com/RandyGaul/cute_framework/commit/ff7f2a58): on the web, a texture ring with no idle slot now reuses its head slot instead of yielding mid-frame. WebGL orders each upload after the draws already issued, so no wait is needed.

## Limits

This is a Chibi build choice, not a general replacement for Cute's asynchronous APIs. Code that calls `emscripten_sleep` needs Asyncify or another way to wait. Cute's WebGPU backend waits that way for its adapter and device, so [cute-web-webgpu-device](cute-web-webgpu-device.md) lets the page provide the device instead.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-web-no-asyncify.patch
git apply cute-web-no-asyncify.patch
```

Patch SHA-256: `c9c12e85f91454ec90b02ac08eb50456bc5466bb7b1dc44f4dd0c2c76576b783`.
