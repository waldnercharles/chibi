# Start WebGPU in the browser without Asyncify

Keep this patch while Chibi's web player links without Asyncify ([cute-web-no-asyncify](cute-web-no-asyncify.md)). Upstream commit `96e6034aa6bc44665856ee480962a9ddab0b87cc` starts its WebGPU backend in the browser by requesting an adapter and a device and then waiting for both with `emscripten_sleep`. That wait needs Asyncify, so without it the player aborts before its first frame.

The patch lets the page request the device first and hand it over as `Module.preinitializedWebGPUDevice`. Cute adopts it through `emscripten_webgpu_get_device()` and reads its adapter name and features from the device. Without a page device, a build that can wait requests one as before. A build that cannot wait fails WebGPU initialization, and Cute falls back to WebGL 2 as it does when a browser lacks WebGPU. `emscripten_has_asyncify()` tells the two builds apart at run time.

The remaining waits change only in a build that cannot wait:

- The staging ceiling no longer blocks for a mapped chunk. Staging grows past the ceiling until the browser returns its maps, which happens between frames.
- The polling-loop yield in `cf_webgpu_end_frame` is skipped. Chibi's player schedules frames with `emscripten_set_main_loop`, so the browser presents when each frame returns.
- `cf_gpu_sync` submits without waiting, like `cf_gpu_submit`.
- Destroying a readback cancels its pending map and frees the readback immediately.
- Shutdown cancels pending staging maps and frees their chunks immediately. A small callback record uses the system heap and survives until the cancelled map callback returns, so it stays valid if the caller resets or restores its CF allocator after teardown.

Browser staging maps use `AllowSpontaneous`, and each frame collects completed maps for reuse. The adopted device belongs to Emdawn's JavaScript instance, while Cute pumps a different native instance; `AllowProcessEvents` therefore leaves those callbacks pending indefinitely. Native builds keep `AllowProcessEvents`.

Chibi's `src/host/web/index.html` requests the device with every adapter feature and the adapter's full limits, which covers what Cute asks for itself. It skips WebGPU when the adapter allows fewer than 48 bytes of color targets per sample, because the 2D material raster writes six RGBA16F targets and Cute cannot make that canvas on a smaller adapter. Cute forwards the adopted device's loss and uncaptured-error events to its backend handlers without replacing the page's listeners. Shutdown removes its error listener and deactivates its loss continuation before releasing resources, so a delayed event cannot affect a replacement app.

`emscripten_webgpu_get_device` is deprecated in emdawnwebgpu but still declared and implemented by the port Emscripten 6.0.10 downloads. If a later port removes it, the page can pass the device through a JavaScript import instead.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-web-webgpu-device.patch
git apply cute-web-webgpu-device.patch
```

Patch SHA-256: `91b35b5fcaea08398f95c78ba8754fd66c4b4c9218f8708e71dc6fc1d5e6b104`.
