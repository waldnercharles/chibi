# Each measured validation command owns one deadline; aggregates are unmeasured.
CHIBI_TEST_GOALS := $(filter test% agent-test agent-gpu-test benchmark,$(MAKECMDGOALS))
ifneq ($(strip $(CHIBI_TEST_GOALS)),)
ifneq ($(words $(MAKECMDGOALS)),1)
$(error Temporary 60-second policy: use one validation goal per invocation; aggregate is unadmitted)
endif
endif

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
WEB_BUILD_DIR ?= build-web
WEB_PROFILE_BUILD_DIR ?= build-web-profile
WEB_THREADS_BUILD_DIR ?= build-web-threads
WEB_WORKERS ?= 4
WEB_CMAKE_FLAGS ?=
FAST_BUILD_DIR ?= $(BUILD_DIR)
JOBS ?= 8
BUILD_TARGET ?= chibi chibi_ctl
VERBOSE ?= 0
export VERBOSE TEST_ARGS AGENT_SELECTOR
MAKEFLAGS += --no-print-directory
QUIET = python3 tools/quiet.py
AGENT_SELECTOR ?= --graph-optimize
TEST_ARGS ?=
TEST_TIMINGS ?= 0
ifneq ($(strip $(TEST_ARGS)),)
ifneq ($(MAKECMDGOALS),test-fast)
$(error TEST_ARGS is only supported with test-fast as the sole make goal)
endif
endif
# Apple Clang 17 does not unregister ASan globals on dlclose. Use installed
# upstream LLVM for native sanitized reload tests, without disabling checks.
NATIVE_TOOLCHAIN_FLAGS ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++)
DEBUG_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
RELEASE_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
BENCHMARK_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
TRACY_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_TRACY=ON -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
SANITIZERS ?= address,undefined
SANITIZER_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=ON -DCHIBI_SANITIZERS=$(SANITIZERS) -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
LOCAL_CMAKE_FLAGS ?= $(SANITIZER_CMAKE_FLAGS)
FAST_CMAKE_FLAGS ?= $(LOCAL_CMAKE_FLAGS)
# AddressSanitizer reserves a large shadow-memory range on macOS. Disable
# Apple's nano allocator before starting instrumented native binaries so the
# allocator does not report a failed nano-zone reservation.
NATIVE_RUNTIME_ENV ?= MallocNanoZone=0

.PHONY: all help build build-debug build-tracy mcp mcp-controller mcp-setup run debug debug-san play build-release run-tracy benchmark web publish-web web-serial web-configure web-serve web-profile web-threads web-threads-serve format check-format test test-full test-tools test-slow test-fast test-list test-compiler-parity test-render clean

all: build

.PHONY: package-native update-game-runtime

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
		'make build-tracy    Build native RelWithDebInfo with Tracy enabled' \
		'make run-tracy      Run the native Tracy profile' \
		'make agent-list     List CPU selectors and bounded GPU capabilities' \
		'make agent-test AGENT_SELECTOR=--graph-optimize  CPU iteration' \
		'make agent-gpu-test Quarantined: unmeasured GPU agent command' \
		'make test           Run essential C regressions under one 60-second deadline' \
		'make test-short     Run essential C regressions under one deadline' \
		'make test-studio-selectors  Validate native selector discovery in C' \
		'make test-policy-inventory  Report temporary test admission policy' \
		'make test-fast TEST_ARGS=--graph-optimize  Run one focused native selector' \
		'make test-list      Report exact admitted commands and quarantined coverage' \
		'make test-tools     Alias for the bounded essential C regressions' \
		'make tools-build    Build native tooling tests and benchmark_trials' \
		'make test-slow      Quarantined: complete native suite and function audit' \
		'make test-full      Quarantined: full tooling and native gate' \
		'make test-compiler-parity  Quarantined: unmeasured compiler parity checks' \
		'make test-render    Quarantined: full GPU and window checks' \
		'make benchmark      Quarantined: unmeasured benchmark aggregate' \
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

build-tracy:
	@$(MAKE) build BUILD_DIR="$(TRACY_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(TRACY_CMAKE_FLAGS)" BUILD_TARGET="chibi chibi_ctl"

run-tracy: build-tracy
	@$(MAKE) update-game-runtime
	env $(NATIVE_RUNTIME_ENV) "$(TRACY_BUILD_DIR)/chibi" --project-root "$(PROJECT)" $(SCENE_ARG) $(ARGS)

benchmark:
	@python3 tools/test_policy.py disabled benchmark

# Web builds generate reflection with the native tool.
web-configure:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build-reflect -- cmake --build "$(BUILD_DIR)" --target reflect --parallel $(JOBS)
	@$(QUIET) configure-web -- sh tools/web_env.sh env CHIBI_NATIVE_BUILD_DIR="$(BUILD_DIR)" sh tools/configure_web.sh "$(WEB_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release -DCHIBI_COLLAB_URL="$(CHIBI_COLLAB_URL)" -DCHIBI_REFLECT_EXECUTABLE=$(CURDIR)/$(BUILD_DIR)/reflect -DCMAKE_C_COMPILER_LAUNCHER=$(CURDIR)/tools/web_compiler.sh -DCMAKE_CXX_COMPILER_LAUNCHER=$(CURDIR)/tools/web_compiler.sh $(WEB_CMAKE_FLAGS)

web: web-serial
	@$(MAKE) web-threads
	@$(QUIET) package_web.py -- python3 tools/package_web.py --serial "$(WEB_BUILD_DIR)" --threads "$(WEB_THREADS_BUILD_DIR)" --output "$(WEB_BUILD_DIR)"

web-serial: web-configure
	@$(QUIET) build -- cmake --build "$(WEB_BUILD_DIR)" --target chibi --parallel $(JOBS)

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

# Test configuration and compilation run outside the measured test deadline.
.PHONY: agent-list agent-test agent-gpu-test test-short test-policy-inventory tools-build test-studio-selectors
agent-list:
	@python3 tools/agent.py list

agent-test:
	@python3 tools/test_policy.py admit-env AGENT_SELECTOR
	@$(MAKE) build BUILD_TARGET=test_suite
	@python3 tools/test_policy.py agent-env "$(BUILD_DIR)" test

agent-gpu-test:
	@python3 tools/test_policy.py disabled agent-gpu-test

test: test-short

TOOLS_BUILD_DIR ?= build-tools
TOOLS_C_COMPILER ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),/opt/homebrew/opt/llvm/bin/clang,$(CC))
tools-build:
	@cmake -S test/tools -B "$(TOOLS_BUILD_DIR)" -DCMAKE_C_COMPILER="$(TOOLS_C_COMPILER)" -DCMAKE_BUILD_TYPE=Debug
	@cmake --build "$(TOOLS_BUILD_DIR)" --parallel $(JOBS)

test-short: tools-build
	@python3 tools/test_policy.py short "$(abspath $(TOOLS_BUILD_DIR))/test_tools"

test-tools: test-short

test-studio-selectors: tools-build
	@$(MAKE) build BUILD_TARGET=test_suite
	@python3 tools/test_policy.py studio-selectors "$(abspath $(TOOLS_BUILD_DIR))/test_tools" "$(abspath $(BUILD_DIR))/test_suite"

test-full test-slow test-render test-compiler-parity:
	@python3 tools/test_policy.py disabled $@

test-fast:
	@python3 tools/test_policy.py admit-env TEST_ARGS
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(FAST_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(FAST_CMAKE_FLAGS)
	@$(QUIET) build -- cmake --build "$(FAST_BUILD_DIR)" --target test_suite --parallel $(JOBS)
	@$(QUIET) test-suite -- env $(NATIVE_RUNTIME_ENV) CHIBI_TEST_TIMINGS=$(TEST_TIMINGS) python3 tools/test_policy.py native-env TEST_ARGS "$(FAST_BUILD_DIR)/test_suite"

test-list test-policy-inventory:
	@python3 tools/test_policy.py inventory

clean:
	@if git worktree list --porcelain | awk -v root="$(abspath $(BUILD_DIR))" \
		'$$1 == "worktree" && ($$2 == root || index($$2, root "/") == 1) { found = 1 } END { exit !found }'; then \
		printf '%s\n' "Skipping $(BUILD_DIR): it contains a registered worktree"; \
	else \
		rm -rf "$(BUILD_DIR)"; \
	fi
	rm -rf "$(DEBUG_BUILD_DIR)" "$(RELEASE_BUILD_DIR)" "$(BENCHMARK_BUILD_DIR)" "$(TRACY_BUILD_DIR)" "$(TOOLS_BUILD_DIR)" "$(WEB_BUILD_DIR)" "$(WEB_PROFILE_BUILD_DIR)" "$(DEV_BUILD_DIR)"

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
