# ADR 0009: cute_framework hosts the app and supplies the ImGui C binding

Status: Accepted
Date: 2026-08-18

## Context

The editor needs a window, an input source, a renderer, and a C binding for
Dear ImGui. Dear ImGui is C++, so a pure C project needs generated bindings.
Two generated bindings exist and both come from dear_bindings: cimgui and
dcimgui.

cute_framework bundles all four pieces. `cf_app_init_imgui()` sets up the ImGui
platform and renderer backends, `dcimgui.h` ships as its C binding for ImGui,
and `samples/imgui.c` in its tree is a working pure-C reference. It also
vendors yyjson internally, which is the same JSON library this project needs.

The constraint that shapes the choice is `SCOPE-1` in docs/spec.md: core must
not include any host, windowing, or rendering header, and must be usable and
testable headlessly. Whatever hosts the app has to stay confined to app/ and
editor/, and `test/self_check.c` has to link against core without it. The
current CMakeLists.txt satisfies this: `core` links nothing but yyjson and the
generated registry, `self_check` links `core` and `demo` only, and `cute`
appears solely on the `editor` and `app` targets.

## Decision

cute_framework hosts the app, pinned by commit in CMakeLists.txt through
FetchContent, and its bundled `dcimgui.h` is the ImGui C binding. No separate
cimgui dependency is added.

yyjson is vendored directly at third_party/yyjson/ rather than taken from
cute_framework's copy, so `core` depends on nothing that pulls in a host.
Vendoring the same library twice is the cost of keeping core host-free, and it
is accepted deliberately.

Core, and therefore every requirement in docs/spec.md outside the `UI` section,
is verified by `test/self_check.c`, which never creates a window.

## Consequences

The host layer stays small: app/main.c owns the frame loop and calls
`editor_draw()`, and that is the whole coupling. Swapping the host later means
rewriting app/ and the ImGui call sites in editor/, not core.

One dependency covers window, input, rendering, and ImGui bindings, so there is
no backend wiring to maintain and no version skew between an ImGui build and a
separately generated binding.

The costs: a pinned FetchContent dependency means the first configure clones a
sizeable repository, and cute_framework's own dependencies come along with it.
`app` needs `LINKER_LANGUAGE CXX` because cute_framework and ImGui are C++,
which is already set in CMakeLists.txt. Upgrading the pin is a deliberate act,
since dcimgui is regenerated with ImGui and its C API can shift.

## Alternatives rejected

### SDL plus cimgui, wired by hand

This is the conventional combination and gives finer control over the ImGui
version. It also means owning the backend wiring, the ImGui build, and the
cimgui generation step, and adding a renderer choice on top. cute_framework
already provides a tested pure-C path to the same place, and the editor gains
nothing from controlling the backend.

### A smaller windowing library such as GLFW or sokol_app

Either one keeps the dependency footprint smaller, but neither ships an ImGui C
binding, so the binding problem stays unsolved and the renderer still has to be
chosen and wired. The footprint saving does not pay for that work while the
host layer remains as thin as app/main.c.

### Terminal or headless-only, with no editor host

Core is already headless and fully tested that way, so this is viable for the
runtime. It is not viable for the product: docs/spec.md `UI-1` requires that
every value the file format can express be authorable in the editor, and a
visual scripting language without a canvas is not the thing being built.
