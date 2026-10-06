# Use an installed SDL3 package

`cute-installed-sdl.patch` adds an optional `CHIBI_SDL3_PACKAGE_PREFIX` CMake setting. When it is set, Cute finds an SDL3 3.4.0 config package at that prefix instead of downloading and compiling SDL3. It requires the exact version and uses `NO_DEFAULT_PATH`, so an unrelated system installation cannot replace the requested package. The normal FetchContent path still works when the setting is empty.

The patch also pins the SDL release archive by SHA-256 and avoids calling `target_include_directories` with an empty `SDL3_INCLUDE_DIRS` value. Installed SDL targets carry their own include paths.

## Why retain it

At audited upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`, [Cute's CMake file](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/CMakeLists.txt#L428-L477) still always populates SDL3 for native builds. It has no package-prefix branch.

Chibi exposes this setting in `cmake/dependencies.cmake`. Its dependency-package relocation code also recognizes the setting. No checked-in configuration currently assigns a nonempty value, so this patch supports an optional build path. It does not fix a failure in Chibi's default source build.

## Example

This configuration example is illustrative. It was not executed during this audit.

```sh
cmake -S . -B build-with-installed-sdl \
  -DCHIBI_SDL3_PACKAGE_PREFIX=/opt/sdl3-3.4.0
```

The prefix must contain an SDL3 CMake config package exporting the target Cute needs. Static Cute builds need `SDL3::SDL3-static`; shared Cute builds use `SDL3::SDL3`.

## Limits

The branch applies only to native builds. Emscripten supplies SDL through its own port.

On Windows, Cute's D3D12 layout generator reads private headers from `sdl3_SOURCE_DIR`. A normal installed SDL package usually omits those source files. This patch does not replace that generator or make an installed package sufficient for Windows. That build still needs the matching SDL source tree and its path.

Validation for this explanation is source inspection. Configuration and compilation results belong to the upgrade audit report.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-installed-sdl.patch
git apply cute-installed-sdl.patch
```

Patch SHA-256: `0cb81936ce2d65fe2a06f3b9dc25f23dd9d08d4459100364a950e28923bb44da`.
