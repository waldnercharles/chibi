# Cute Framework upgrade and patch audit

Audited on October 6, 2026. The dependency now targets [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d), the current `master` commit observed during this audit. The previous pin was `a13f4661f5ba27e8d45027b52f121c1704786425`. The latest tagged release is still 1.1.1, which predates the previous pin, so the update follows the existing commit-pinning approach.

Of 22 patch files, four fixes are now upstream and one unused API can be removed. Seventeen patches remain. The float-readback patch is narrower because upstream already handles most format-specific readback. The other rebases preserve the carried behavior while using the new source layout.

Each retained patch has an unlisted GitHub gist containing a README and the exact patch used by the build. The README explains the change, its trigger, the Chibi use, backend limits, and how to apply it. Examples illustrate the relevant contract; they are not new runtime reproductions.

## Removed patches

| Patch | Decision and evidence |
| --- | --- |
| `cute-app-exists` | No current Chibi source, authored asset, or test caller uses `cf_app_exists` or its C++ wrapper. This is an unused local API, not an upstream fix. |
| `cute-draw-samplers` | Superseded by [9855683b](https://github.com/RandyGaul/cute_framework/commit/9855683bac3d4892c9059d77b57d15cf1cca88f4). Upstream preserves custom samplers and applies draw filtering only to `u_image`, with later GLES anisotropy improvements. |
| `cute-gles-ring-reuse` | Exact fix incorporated by [59ffd91e](https://github.com/RandyGaul/cute_framework/commit/59ffd91e4d76c0b5ea602a8d35111c6924cd1376). It clears a slot's in-flight frame after its emergency fence signals. |
| `cute-gles-storage-samplers` | Superseded by [d1fd883d](https://github.com/RandyGaul/cute_framework/commit/d1fd883d64e5ecf4c4aeb0a85cb5230fae25f7ec). Upstream reserves high texture units for emulated storage and checks material collisions. New compute code uses compatible allocation. |
| `cute-png-zlib-window` | Exact fix incorporated by [3bd0e40d](https://github.com/RandyGaul/cute_framework/commit/3bd0e40d26785d2317b26667c0726f68fb504064). Saved PNGs advertise the 32 KiB deflate window. |

## Retained patches and gists

The rows follow the build's application order. Sixteen patches apply independently to the audited commit. The custom-shape cache requires the coverage patch because it uses that patch's registration helper and coverage flag. The complete ordered series also applies cleanly.

The counts and checks on this page cover the audit. One patch added since, `cute-sound-free-queue-music-volume`, applies independently and has no gist yet.

| Patch and gist | Why it remains |
| --- | --- |
| [cute-installed-sdl](https://gist.github.com/waldnercharles/69885ff04ff601e04c67658722d81713) | Optional use of an exact installed SDL3 package. |
| [cute-interior-border](https://gist.github.com/waldnercharles/ce46b51fd93a3472ecfbc89fbeedf34a) | Interior borders without enlarging the shape silhouette. |
| [cute-custom-coverage](https://gist.github.com/waldnercharles/d43c4946532037526135b7ea258e422d) | Safe tile coverage for warped fields that are not distance bounds. |
| [cute-custom-shape-cache](https://gist.github.com/waldnercharles/97b6354625dd85fce1e3b5a12cff5651) | Reuse identical custom-shape registrations and avoid recompilation. |
| [cute-imgui-render-target](https://gist.github.com/waldnercharles/b1b973b262e2dd6c70875e79b057b8ce) | Include native ImGui overlays in canvas captures. |
| [cute-gles-buffer-streaming](https://gist.github.com/waldnercharles/0472941113287f7561f53199398c645d) | Append or orphan streaming buffers instead of waiting on fences. |
| [cute-zero-size-window](https://gist.github.com/waldnercharles/aaee4edae0cdb28bb666ff8a3b77501c) | Preserve valid canvases through zero-area window transitions. |
| [cute-gles-scissor-cache](https://gist.github.com/waldnercharles/5620f89c7d0a286199066cc9eb7ae3df) | Keep cached scissor state equal to the box actually sent to GL. |
| [cute-gpu-submit](https://gist.github.com/waldnercharles/9cd0885921a9c423de18eacb46e8969f) | Submit GPU profiler stages without waiting on the CPU. |
| [cute-eight-targets](https://gist.github.com/waldnercharles/f89bafbe4bd9dfc9920819d2e420cff8) | Allow the five- and six-output rendering passes on capable devices. |
| [cute-gles-float-readback](https://gist.github.com/waldnercharles/7d9220eb49f660019522cb13d338b503) | Read RGBA16F through the portable GLES float pair, then pack halves. |
| [cute-web-no-asyncify](https://gist.github.com/waldnercharles/cc8dea574f7210c5ba74ad78f99b57f4) | Use Chibi browser callbacks without Asyncify suspension. |
| [cute-fragment-depth](https://gist.github.com/waldnercharles/d1403f5d46ec2bcdebb21868ecbf2476) | Compile shader-written SDF hit depth. |
| [cute-gles-ubo-instance-scope](https://gist.github.com/waldnercharles/f8e2691d98e157dc2a1fa5812818ea9b) | Give anonymous uniform block instances separate stage names. |
| [cute-instance-buffer](https://gist.github.com/waldnercharles/6d3532d8380b80ab5d6f8a304b10bc12) | Expose existing shared instance staging through the public C header. |
| [cute-depth-snapshot](https://gist.github.com/waldnercharles/35405925d7c0b7f9a4b4645f35337b76) | Copy depth for later SDF visibility tests. |
| [cute-sanitizer-cleanup](https://gist.github.com/waldnercharles/6e73081d5c96eed112d9ba64210d30a2) | Release shader, material, draw, font and compute allocations. |
| [cute-sound-free-queue-music-volume](cute-sound-free-queue-music-volume.md) | Grow the deferred free queue by pointers and apply the music volume once. |

## Partial upstream work

[b945aa36](https://github.com/RandyGaul/cute_framework/commit/b945aa36885f78974b4c3bec419b32566e4440b2) made readback format-aware. The remaining local change is the RGBA16F float read with conversion back to half-floats. Other upstream readback formats and error handling stay intact.

[625e5442](https://github.com/RandyGaul/cute_framework/commit/625e5442f11db43e81369a2cea94dfc36449a770) separately clears the global draw pointer on shutdown. The cleanup patch preserves that fix and adds the allocation and ownership corrections that upstream still lacks. The local cleanup patch did not originally include the draw-pointer fix.

## Build choices and API additions

Retention does not mean every patch fixes an upstream bug. Installed SDL is an optional native build path. Disabling Asyncify is a Chibi scheduling choice. Interior borders, coverage registration, GPU submission, more canvas color targets, public instance buffers and depth snapshots add capabilities Chibi uses. Their gists describe the contract and limits.

The inline Cute integration settings in `cmake/dependencies.cmake` also remain necessary: SDL/s2n Git source overrides, physics Wasm SIMD policy, deterministic floating-point compiler options, the Apple SDK compatibility flag, ImGui character/index ABI definitions, and the private yyjson symbol prefix. Latest upstream has no equivalent for those settings. Its new Emscripten ImGui ES3 definition already covers that upstream requirement.

## Verification

Patch applicability passed for every independent patch and the cache with its prerequisite. The entire 17-patch series passed `git apply` and `git diff --check`. Patch SHA-256 values and gist revisions are recorded in `gists.json`. Every uploaded README and patch is read back through the GitHub API and compared with the local file.

`make build BUILD_TARGET='chibi chibi_ctl test_suite'` passed with Clang 23.1.1 and address/undefined sanitizers enabled. Final native configuration took 0.8 seconds and application compilation took 44.2 seconds after the dependency package had been prepared. `make web` passed for serial and four-worker threaded builds with Emscripten 6.0.9, and browser packaging completed. Final serial dependency preparation/configuration took 72.9 seconds and app compilation took 61.3 seconds. The unchanged threaded build completed in 1.0 second. These are build durations, separate from test execution.

The admitted C tooling suite passed 17 tests and 188 assertions in 19.461 seconds under its shared 60-second deadline. These tests cover tooling and execution policy, not the Cute renderer. Final native, serial web and threaded web dependency snapshots match all 17 published patch hashes and the new upstream pin. `verification.json` records the commands, timings and remaining coverage.

The repository's temporary test policy quarantines native/render aggregates and GPU selectors. Those checks remain disabled and unverified. No new rendered capture, browser transition, readback reproduction, sanitizer leak run, or GPU benchmark is claimed.

Build retirement metadata capture skipped its Ninja query for the Makefile-based web builds. This did not fail compilation or browser packaging.
