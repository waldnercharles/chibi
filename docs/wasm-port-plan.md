# WebAssembly port plan

Goal: ship the current movement/editor app as a browser-hosted WASM application without changing native behavior or weakening the native test/build path.

## Constraints and decisions

- Target Emscripten + SDL3 + cute_framework's GLES3/WebGL2 backend.
- Keep `core`, `ecs`, `game`, and editor model code host-independent.
- Keep native builds and native SLJIT execution unchanged.
- The first web target uses typed/generic bytecode, not SLJIT.
- Treat the browser as a client: bundled assets are read-only initially; saving and downloads are a separate feature.
- Native screenshot/headless diagnostics remain native-only.

## Incremental check-ins

Each step must build the native app and pass the existing test suite before moving on.

1. **Web configuration and portability guards** — complete
   - Emscripten disables SLJIT and uses a minimal stacktrace implementation.
   - Native build remains validated.

   - Add a documented web build option/toolchain entry point.
   - Disable SLJIT automatically for Emscripten.
   - Add web-safe stacktrace/assertion handling.
   - Keep all native behavior unchanged.

2. **Host-tool separation** — complete
   - Cross builds accept `CBLUEPRINT_REFLECT_EXECUTABLE` and do not create a WASM `reflect` target.
   - Native builds continue to build `reflect` normally.
   - A web configure can use a host tool from `build/reflect`, for example:
     `emcmake cmake -S . -B build-web -DCBLUEPRINT_REFLECT_EXECUTABLE=$PWD/build/reflect`.

   - Make reflection/registry generation a native host-tool step.
   - Support supplying generated registry C files to a cross build.
   - Ensure the web build never tries to link or execute native `libclang` code as WASM.

3. **Browser frame lifecycle** — complete
   - Extract one frame from `app/main.c` into a single update function.
   - Use `emscripten_set_main_loop` only for web; retain the native `while` loop.
   - Make cleanup and error paths explicit for both lifecycles.
   - Reject screenshot capture on web; browser builds use the continuous loop.

4. **First browser artifact** — in progress
   - A `make web` target now builds a browser artifact and `make web-serve` serves it.
   - `.github/workflows/deploy-web.yml` builds and deploys the artifact to GitHub Pages.
   - Preload `content/` and required fonts/assets.
   - Link the app as HTML/JS/WASM with WebGL2, memory growth, and suitable optimization flags.
   - Add a minimal static-server/deployment script and verify the editor renders in a browser.

5. **Browser interaction and layout**
   - Verify keyboard, mouse, focus routing, canvas resizing, DPI, and viewport recreation.
   - Add browser-specific canvas sizing/fullscreen behavior if needed.
   - Test Chrome, Firefox, and Safari where practical.

6. **Web persistence and export**
   - Decide whether saves use IndexedDB, downloads, or the File System Access API.
   - Replace native temporary-file/rename assumptions in the web path.
   - Add import/export of behavior_asset JSON as an explicit browser feature.

7. **Performance and release hardening**
   - Measure typed/generic bytecode against native SLJIT.
   - Reduce artifact size and startup time.
   - Add a release CI job and verify correct WASM MIME types, compression, and cache headers.

## WASM JIT decision

There is no practical way to use the current SLJIT backend as a browser JIT. SLJIT emits native machine instructions into executable memory; WebAssembly cannot execute arbitrary machine code stored in its linear memory, and browsers enforce W^X-style restrictions.

A browser can compile and instantiate a new WebAssembly module at runtime through the JavaScript `WebAssembly.compile`/`instantiate` APIs. In theory we could create a new WASM code generator and a JS/WASM bridge for graph programs, or generate all graph code ahead of time. That is a separate backend, not a switch that makes SLJIT work. It is out of scope for the first port; bytecode is the portable fallback.

## Acceptance criteria

- `cmake --build build` and the existing native tests remain green.
- A clean Emscripten configure/build produces `.html`, `.js`, and `.wasm` artifacts.
- Opening the HTML through a local HTTP server displays and interacts with the current editor.
- Bundled movement content loads and the movement graph executes using bytecode.
- No browser path relies on POSIX stack unwinding, native executable memory, or native file dialogs.
