# Native desktop setup commands can run before a SessionStart hook.
# Enrollment is silent and creates no state for repositories without installation.
ifeq ($(wildcard .chibi-workspace.json),)
ifneq ($(wildcard tools/workspaces.py),)
ifneq ($(shell python3 tools/workspaces.py enroll >/dev/null && printf ready),ready)
$(error Workspace enrollment failed; inspect the workspaces error above)
endif
endif
endif

# Managed workspaces hold one lock across configure, tests, and launched runtimes.
# Recursive make inherits MAKEFLAGS, command-line variables, and the lock fd.
override CHIBI_WORKSPACE_INNER := 0
ifneq ($(shell test -f .chibi-workspace.json && printf managed),)
ifeq ($(strip $(CHIBI_WORKSPACE_LOCK_FD)),)
CHIBI_WORKSPACE_GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)
.DEFAULT_GOAL := all
.PHONY: $(CHIBI_WORKSPACE_GOALS) .chibi-workspace-invocation
$(CHIBI_WORKSPACE_GOALS): .chibi-workspace-invocation
.chibi-workspace-invocation:
ifneq ($(findstring q,$(firstword $(MAKEFLAGS))),)
# Every delegated convenience goal is phony and therefore pending in query mode.
	@:
else
	+@python3 tools/workspaces.py run -- $(MAKE) $(CHIBI_WORKSPACE_GOALS)
endif
else
ifneq ($(shell python3 tools/workspaces.py validate-lock >/dev/null 2>&1 && printf valid),valid)
$(error Invalid inherited workspace lock; run make without CHIBI_WORKSPACE_LOCK_FD)
endif
override CHIBI_WORKSPACE_INNER := 1
endif
else
override CHIBI_WORKSPACE_INNER := 1
endif

ifeq ($(CHIBI_WORKSPACE_INNER),1)
# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR ?= build
DEBUG_BUILD_DIR ?= build-debug
BENCHMARK_BUILD_DIR ?= build-benchmark
RELEASE_BUILD_DIR ?= build-release
PACKAGE_BUILD_DIR ?= build-package
PACKAGE_OUTPUT ?= dist/chibi-$(shell uname -s | tr '[:upper:]' '[:lower:]')-$(shell uname -m)
UPDATE_GAME_RUNTIME ?= 1
TRACY_BUILD_DIR ?= build-tracy
TRACY_MCP_BUILD_DIR ?= build-tracy-mcp
WEB_BUILD_DIR ?= build-web
WEB_PROFILE_BUILD_DIR ?= build-web-profile
WEB_THREADS_BUILD_DIR ?= build-web-threads
WEB_WORKERS ?= 4
WEB_CMAKE_FLAGS ?=
FAST_BUILD_DIR ?= build-debug
JOBS ?= 8
BUILD_TARGET ?= chibi chibi_ctl
VERBOSE ?= 0
export VERBOSE
MAKEFLAGS += --no-print-directory
QUIET = python3 tools/quiet.py
TEST_ARGS ?=
# Apple Clang 17 does not unregister ASan globals on dlclose. Use installed
# upstream LLVM for native sanitized reload tests, without disabling checks.
NATIVE_TOOLCHAIN_FLAGS ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++)
DEBUG_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
RELEASE_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
BENCHMARK_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
TRACY_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_TRACY=ON -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
TRACY_MCP_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_TRACY=ON -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON -DTRACY_CLIENT_PYTHON=ON -DTRACY_STATIC=OFF
TRACY_MCP_PYTHON ?= python3
SANITIZERS ?= address,undefined
SANITIZER_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=ON -DCHIBI_SANITIZERS=$(SANITIZERS) -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
LOCAL_CMAKE_FLAGS ?= $(SANITIZER_CMAKE_FLAGS)
# AddressSanitizer reserves a large shadow-memory range on macOS. Disable
# Apple's nano allocator before starting instrumented native binaries so the
# allocator does not report a failed nano-zone reservation.
NATIVE_RUNTIME_ENV ?= MallocNanoZone=0

.PHONY: all help build build-debug build-tracy build-tracy-mcp mcp mcp-controller mcp-setup tracy-mcp run debug debug-san play build-release run-tracy benchmark web publish-web web-serial web-configure web-serve web-profile web-threads web-threads-serve profile-compare format check-format test test-fast test-render clean

all: build

.PHONY: package-native test-package update-game-runtime

help:
	@printf '%s\n' \
		'make all            Alias for make build' \
		'make help           Show this target summary' \
		'make build          Build Debug app/controller; VERBOSE=1 streams logs' \
		'make build-debug    Build the plain Debug profile' \
		'make run            Run the optimized editor on PROJECT=projects/<name> [SCENE=scenes/<file>]' \
		'make debug          Run the plain Debug editor' \
		'make debug-san      Run Debug with ASan+UBSan; use SANITIZERS=thread for TSan' \
		'make play           Run the optimized Arena2D game' \
		'make build-release  Build the fully optimized native profile' \
		'make package-native Build a relocatable engine, Studio, and MCP package' \
		'make update-game-runtime Refresh registered games explicitly' \
		'make test-package   Verify the native package against an external game' \
		'make build-tracy    Build native RelWithDebInfo with Tracy enabled' \
		'make run-tracy      Run the native Tracy profile' \
		'make tracy-mcp      Build and run the official Tracy MCP server' \
		'make test           Run generation, MCP, runtime, and headless checks' \
		'make test-fast TEST_ARGS=--graph-optimize  Run one focused plain Debug check' \
		'make test-render    Run GPU previews and hidden-window checks' \
		'make benchmark      Run optimized benchmarks with symbols' \
		'make dev            Run the RelWithDebInfo source reload host' \
		'make mcp            Run the MCP server over stdio' \
		'make mcp-controller Build only the chibi_ctl MCP controller' \
		'make mcp-setup      Generate local Codex MCP configuration' \
		'make web            Build paired serial and worker browser applications' \
		'make publish-web    Publish the latest origin/main commit to GitHub Pages' \
		'make web-serve      Serve the browser application for PROJECT on WEB_PORT' \
		'make web-profile    Serve a web build named for the browser profiler' \
		'make web-serial     Build only the serial browser application' \
		'make web-threads    Build only the worker browser application' \
		'make web-threads-serve Serve the threaded application with isolation headers' \
		'make profile-compare Profile PROJECT headless natively and as WASM' \
		'make format         Rewrite project C sources with clang-format' \
		'make check-format   Check project C source formatting' \
		'make clean          Remove the default build directories'

build:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build -- cmake --build "$(BUILD_DIR)" $(if $(strip $(BUILD_TARGET)),--target $(BUILD_TARGET),) --parallel $(JOBS)
# The MCP controller cache keys on sources alone: publish only default-profile
# builds that refreshed chibi_ctl.
ifeq ($(origin NATIVE_TOOLCHAIN_FLAGS) $(origin LOCAL_CMAKE_FLAGS) $(origin SANITIZER_CMAKE_FLAGS) $(origin SANITIZERS),file file file file)
ifneq ($(filter chibi_ctl,$(BUILD_TARGET))$(if $(strip $(BUILD_TARGET)),,all),)
	@sh tools/mcp/controller-cache.sh publish "$(BUILD_DIR)/chibi_ctl" || true
endif
endif

build-debug:
	@$(MAKE) build BUILD_DIR="$(DEBUG_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(DEBUG_CMAKE_FLAGS)" BUILD_TARGET="$(BUILD_TARGET)"

mcp-controller:
	@$(QUIET) mcp-build -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) mcp-build -- cmake --build "$(BUILD_DIR)" --target chibi_ctl --parallel $(JOBS)

mcp: mcp-controller
	@printf '%s\n' 'MCP server running on stdio. Paste one JSON-RPC request per line, or press Ctrl-D to stop.' >&2
	@sh tools/mcp/launch.sh

mcp-setup:
	node tools/mcp/setup-codex.mjs "$(CURDIR)"

# PROJECT picks the project a run or web-serve opens; SCENE, when set, opens a
# scene other than the project's start scene.
PROJECT ?= projects/arena2d
SCENE ?=
SCENE_ARG = $(if $(SCENE),--scene "$(SCENE)")
WEB_PORT ?= 8099
CHIBI_COLLAB_URL ?=

run:
	@$(MAKE) build-release BUILD_TARGET=chibi
	@$(MAKE) update-game-runtime
	env $(NATIVE_RUNTIME_ENV) "$(RELEASE_BUILD_DIR)/chibi" --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

debug:
	@$(MAKE) build-debug BUILD_TARGET=chibi
	@$(MAKE) update-game-runtime
	env $(NATIVE_RUNTIME_ENV) "$(DEBUG_BUILD_DIR)/chibi" --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

debug-san:
	@$(MAKE) build BUILD_TARGET=chibi
	@$(MAKE) update-game-runtime
	env $(NATIVE_RUNTIME_ENV) "$(BUILD_DIR)/chibi" --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

play:
	@$(MAKE) build-release BUILD_TARGET=chibi
	@$(MAKE) update-game-runtime
	env $(NATIVE_RUNTIME_ENV) "$(RELEASE_BUILD_DIR)/chibi" --play --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

# User-facing release builds use the compiler's full Release optimization.
build-release:
	@$(MAKE) build BUILD_DIR="$(RELEASE_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS)" BUILD_TARGET="$(BUILD_TARGET)"

update-game-runtime:
ifneq ($(wildcard .chibi/game-runtime.json),)
ifeq ($(UPDATE_GAME_RUNTIME),1)
	@$(MAKE) package-native UPDATE_GAME_RUNTIME=0
endif
endif

package-native:
	@$(MAKE) build BUILD_DIR="$(PACKAGE_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS) -DCHIBI_DEVELOPMENT_RELOAD=OFF -DCHIBI_DISTRIBUTION=ON" BUILD_TARGET="chibi chibi_ctl" UPDATE_GAME_RUNTIME=0
	@$(QUIET) package_native.py -- python3 tools/package_native.py --build "$(PACKAGE_BUILD_DIR)" --output "$(PACKAGE_OUTPUT)"
	@$(QUIET) install_game_runtime.py -- python3 tools/install_game_runtime.py --package "$(PACKAGE_OUTPUT)"

test-package: package-native
	@$(QUIET) package_native_test.py -- python3 test/package_native_test.py --package "$(PACKAGE_OUTPUT)"
	@$(QUIET) mcp-package-tests -- node --test test/mcp_package_test.mjs

build-tracy:
	@$(MAKE) build BUILD_DIR="$(TRACY_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(TRACY_CMAKE_FLAGS)" BUILD_TARGET="chibi chibi_ctl"

run-tracy: build-tracy
	@$(MAKE) update-game-runtime
	env $(NATIVE_RUNTIME_ENV) "$(TRACY_BUILD_DIR)/chibi" --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

build-tracy-mcp:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(TRACY_MCP_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(TRACY_MCP_CMAKE_FLAGS) -DPython_EXECUTABLE="$(TRACY_MCP_PYTHON)" $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build -- cmake --build "$(TRACY_MCP_BUILD_DIR)" --target TracyServerBindings --parallel $(JOBS)

tracy-mcp: build-tracy-mcp
	TRACY_MCP_BUILD_DIR="$(TRACY_MCP_BUILD_DIR)" TRACY_MCP_PYTHON="$(TRACY_MCP_PYTHON)" sh tools/tracy/launch_mcp.sh

benchmark:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BENCHMARK_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(BENCHMARK_CMAKE_FLAGS)
	@$(QUIET) build -- cmake --build "$(BENCHMARK_BUILD_DIR)" --target bench_activation system_benchmark query_benchmark contact_benchmark --parallel $(JOBS)
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/bench_activation" $(ARGS)
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/system_benchmark" $(ARGS)
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/query_benchmark"
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/contact_benchmark"

# Web builds generate reflection with the native tool.
web-configure:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build-reflect -- cmake --build "$(BUILD_DIR)" --target reflect --parallel $(JOBS)
	@$(QUIET) configure-web -- sh tools/configure_web.sh "$(WEB_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release -DCHIBI_COLLAB_URL="$(CHIBI_COLLAB_URL)" -DCHIBI_REFLECT_EXECUTABLE=$(CURDIR)/$(BUILD_DIR)/reflect $(WEB_CMAKE_FLAGS)

web: web-serial
	@$(MAKE) web-threads
	@$(QUIET) package_web.py -- python3 tools/package_web.py --serial "$(WEB_BUILD_DIR)" --threads "$(WEB_THREADS_BUILD_DIR)" --output "$(WEB_BUILD_DIR)"

web-serial: web-configure
	@$(QUIET) build -- cmake --build "$(WEB_BUILD_DIR)" --target chibi check-behavior-backend --parallel $(JOBS)

# Profile PROJECT's start scene (or PROFILE_SCENE, or PROFILE_SCENARIO's replayed
# gameplay) headless, natively and as WASM under Node, with the same fixed steps.
profile-compare: web-configure
	@$(MAKE) build-release BUILD_TARGET=chibi_ctl
	@$(QUIET) build -- cmake --build "$(WEB_BUILD_DIR)" --target chibi_ctl --parallel $(JOBS)
	python3 tools/profile_compare.py --project-root "$(PROJECT)" --native "$(RELEASE_BUILD_DIR)/chibi_ctl" --wasm "$(WEB_BUILD_DIR)/chibi_ctl.js" $(if $(PROFILE_SCENE),--scene "$(PROFILE_SCENE)") $(if $(PROFILE_SCENARIO),--scenario "$(PROFILE_SCENARIO)") $(PROFILE_ARGS)

publish-web:
	python3 tools/publish_web.py --jobs "$(JOBS)" --collab-url "$(CHIBI_COLLAB_URL)"

web-serve: web
	python3 tools/web_serve.py --build "$(WEB_BUILD_DIR)" --project "$(PROJECT)" --port $(WEB_PORT)

# Serve a web build whose WASM functions and trace zones are named in the
# browser's Performance panel.
web-profile:
	@$(MAKE) web-serve WEB_BUILD_DIR="$(WEB_PROFILE_BUILD_DIR)" WEB_CMAKE_FLAGS=-DCHIBI_WEB_PROFILING=ON

web-threads:
	@$(MAKE) web-serial WEB_BUILD_DIR="$(WEB_THREADS_BUILD_DIR)" WEB_CMAKE_FLAGS="-DCHIBI_WEB_THREADS=ON -DCHIBI_WEB_WORKERS=$(WEB_WORKERS) $(WEB_CMAKE_FLAGS)"

web-threads-serve: web
	python3 tools/web_serve.py --build "$(WEB_BUILD_DIR)" --project "$(PROJECT)" --port $(WEB_PORT) --isolated

format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh

check-format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh --check

test:
	@$(QUIET) kdl_asset_test.py -- python3 test/kdl_asset_test.py
	@$(QUIET) fetchcontent_cache_test.py -- python3 test/fetchcontent_cache_test.py
	@$(QUIET) configure_cache_test.py -- python3 test/configure_cache_test.py
	@$(QUIET) dependency_package_test.py -- python3 test/dependency_package_test.py
	@$(QUIET) package_web_test.py -- python3 test/package_web_test.py
	@$(QUIET) publish_web_test.py -- python3 test/publish_web_test.py
	@$(QUIET) package_native_test.py -- python3 test/package_native_test.py
	@$(QUIET) install_game_runtime_test.py -- python3 test/install_game_runtime_test.py
	@$(QUIET) runtime_update_build_test.py -- python3 test/runtime_update_build_test.py
	@$(QUIET) quiet_test.py -- python3 test/quiet_test.py
	@$(QUIET) workspaces_test.py -- python3 test/workspaces_test.py
	@$(QUIET) workspace_retirement_test.py -- python3 test/workspace_retirement_test.py
	@$(QUIET) workspace_make_test.py -- python3 test/workspace_make_test.py
	@$(QUIET) workspace_install_test.py -- python3 test/workspace_install_test.py
	@$(QUIET) mcp-package-tests -- node --test test/mcp_package_test.mjs
	@$(QUIET) web_serve_test.py -- python3 test/web_serve_test.py
	@$(QUIET) web_workspace_test.mjs -- node test/web_workspace_test.mjs
	@$(QUIET) web_storage_test.mjs -- node test/web_storage_test.mjs
	@$(QUIET) web_frames_test.mjs -- node test/web_frames_test.mjs
	@$(QUIET) web_isolation_test.mjs -- node test/web_isolation_test.mjs
	@$(QUIET) collab_client_test.mjs -- node test/collab_client_test.mjs
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS)
	@$(QUIET) build -- cmake --build "$(BUILD_DIR)" --target check-generation check-reflect check-reload check-dev check-mcp check-assets check-behavior-backend test_suite --parallel $(JOBS)
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) $(BUILD_DIR)/test_suite
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --whole-function-audit

test-fast:
	@test -n "$(strip $(TEST_ARGS))" || { printf '%s\n' 'Set TEST_ARGS to a test_suite selector, such as --graph-optimize'; exit 2; }
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(FAST_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(DEBUG_CMAKE_FLAGS)
	@$(QUIET) build -- cmake --build "$(FAST_BUILD_DIR)" --target test_suite --parallel $(JOBS)
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) "$(FAST_BUILD_DIR)/test_suite" $(TEST_ARGS)

test-render:
	@if [ "$(shell uname -s)" = Darwin ] && [ "$(CHIBI_ALLOW_DESKTOP_RENDER_TESTS)" != 1 ]; then \
		printf '%s\n' 'Render tests can briefly show a window on macOS. Run them under Xvfb on Linux, or set CHIBI_ALLOW_DESKTOP_RENDER_TESTS=1 for a local run.'; \
		exit 1; \
	fi
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS)
	@$(QUIET) build -- cmake --build "$(BUILD_DIR)" --target check-mcp-gpu test_suite chibi --parallel $(JOBS)
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --scene-documents
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --shader-detail-gpu
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --studio-documents
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --rmlui-render
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --rmlui-render-gles
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --arena-play
	env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/chibi --headless --frames 3

clean:
	@if git worktree list --porcelain | awk -v root="$(abspath $(BUILD_DIR))" \
		'$$1 == "worktree" && ($$2 == root || index($$2, root "/") == 1) { found = 1 } END { exit !found }'; then \
		printf '%s\n' "Skipping $(BUILD_DIR): it contains a registered worktree"; \
	else \
		rm -rf "$(BUILD_DIR)"; \
	fi
	rm -rf "$(DEBUG_BUILD_DIR)" "$(RELEASE_BUILD_DIR)" "$(BENCHMARK_BUILD_DIR)" "$(TRACY_BUILD_DIR)" "$(TRACY_MCP_BUILD_DIR)" "$(WEB_BUILD_DIR)" "$(WEB_PROFILE_BUILD_DIR)" "$(DEV_BUILD_DIR)"

# Watch sources and preserve the workspace across native application changes.
.PHONY: dev
DEV_BUILD_DIR ?= build-dev
DEV_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON

dev:
	@$(MAKE) build BUILD_DIR="$(DEV_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(DEV_CMAKE_FLAGS) -DCHIBI_DEVELOPMENT_RELOAD=ON" BUILD_TARGET="chibi chibi_dll"
	@$(MAKE) update-game-runtime
	cmake -DDEV_CONFIG="$(abspath $(DEV_BUILD_DIR))/dev.json" -DRESULT="$(abspath $(DEV_BUILD_DIR))/dev-result.json" -DSKIP_BUILD=ON -DJOBS=$(JOBS) -P cmake/dev_build.cmake
	env $(NATIVE_RUNTIME_ENV) "$(DEV_BUILD_DIR)/chibi" --bootstrap "$(abspath $(DEV_BUILD_DIR))/dev-result.json" --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

endif # CHIBI_WORKSPACE_INNER
