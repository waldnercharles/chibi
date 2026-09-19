# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR ?= build
DEBUG_BUILD_DIR ?= build-debug
BENCHMARK_BUILD_DIR ?= build-benchmark
RELEASE_BUILD_DIR ?= build-release
TRACY_BUILD_DIR ?= build-tracy
TRACY_MCP_BUILD_DIR ?= build-tracy-mcp
WEB_BUILD_DIR ?= build-web
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

.PHONY: all help build build-debug build-tracy build-tracy-mcp mcp mcp-setup tracy-mcp run debug debug-san play build-release run-tracy benchmark web web-serve format check-format test clean

all: build

help:
	@printf '%s\n' \
		'make all            Alias for make build' \
		'make help           Show this target summary' \
		'make build          Build the sanitizer-enabled Debug profile' \
		'make build-debug    Build the plain Debug profile' \
		'make run            Run the optimized editor' \
		'make debug          Run the plain Debug editor' \
		'make debug-san      Run Debug with ASan+UBSan; use SANITIZERS=thread for TSan' \
		'make play           Run the optimized Arena2D game' \
		'make build-release  Build the fully optimized native profile' \
		'make build-tracy    Build native RelWithDebInfo with Tracy enabled' \
		'make run-tracy      Run the native Tracy profile' \
		'make tracy-mcp      Build and run the official Tracy MCP server' \
		'make test           Run generation, MCP, runtime, and headless checks' \
		'make benchmark      Run optimized benchmarks with symbols' \
		'make dev            Run the RelWithDebInfo source reload host' \
		'make mcp            Run the MCP server over stdio' \
		'make mcp-setup      Generate local Codex MCP configuration' \
		'make web            Build the browser application' \
		'make web-serve      Serve the browser application locally' \
		'make format         Rewrite project C sources with clang-format' \
		'make check-format   Check project C source formatting' \
		'make clean          Remove the default build directories'

build:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",) && cmake --build $(BUILD_DIR)

build-debug:
	$(MAKE) build BUILD_DIR="$(DEBUG_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(DEBUG_CMAKE_FLAGS)"

mcp:
	@cmake -S . -B "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",) >&2
	@cmake --build "$(BUILD_DIR)" --target chibi_ctl >&2
	@printf '%s\n' 'MCP server running on stdio. Paste one JSON-RPC request per line, or press Ctrl-D to stop.' >&2
	@sh tools/mcp/launch.sh

mcp-setup:
	node tools/mcp/setup-codex.mjs "$(CURDIR)"

PROJECT ?= projects/arena2d
SCENE ?= scenes/Arena2D.json

run: build-release
	env $(NATIVE_RUNTIME_ENV) "$(RELEASE_BUILD_DIR)/chibi" --project-root "$(PROJECT)" --scene "$(SCENE)" $(ARGS)

debug: build-debug
	env $(NATIVE_RUNTIME_ENV) "$(DEBUG_BUILD_DIR)/chibi" --project-root "$(PROJECT)" --scene "$(SCENE)" $(ARGS)

debug-san: build
	env $(NATIVE_RUNTIME_ENV) "$(BUILD_DIR)/chibi" --project-root "$(PROJECT)" --scene "$(SCENE)" $(ARGS)

play: build-release
	env $(NATIVE_RUNTIME_ENV) "$(RELEASE_BUILD_DIR)/chibi" --play --project-root "$(PROJECT)" --scene "$(SCENE)" $(ARGS)

# User-facing release builds use the compiler's full Release optimization.
build-release:
	$(MAKE) build BUILD_DIR="$(RELEASE_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS)"

build-tracy:
	cmake -S . -B "$(TRACY_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(TRACY_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	cmake --build "$(TRACY_BUILD_DIR)" --target chibi chibi_ctl

run-tracy: build-tracy
	env $(NATIVE_RUNTIME_ENV) "$(TRACY_BUILD_DIR)/chibi" --project-root "$(PROJECT)" --scene "$(SCENE)" $(ARGS)

build-tracy-mcp:
	cmake -S . -B "$(TRACY_MCP_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(TRACY_MCP_CMAKE_FLAGS) -DPython_EXECUTABLE="$(TRACY_MCP_PYTHON)" $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	cmake --build "$(TRACY_MCP_BUILD_DIR)" --target TracyServerBindings

tracy-mcp: build-tracy-mcp
	TRACY_MCP_BUILD_DIR="$(TRACY_MCP_BUILD_DIR)" TRACY_MCP_PYTHON="$(TRACY_MCP_PYTHON)" sh tools/tracy/launch_mcp.sh

benchmark:
	cmake -S . -B "$(BENCHMARK_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(BENCHMARK_CMAKE_FLAGS)
	cmake --build "$(BENCHMARK_BUILD_DIR)" --target bench_activation scene_benchmark system_benchmark query_benchmark contact_benchmark
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/bench_activation" $(ARGS)
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/scene_benchmark"
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/system_benchmark" $(ARGS)
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/query_benchmark"
	env $(NATIVE_RUNTIME_ENV) "$(BENCHMARK_BUILD_DIR)/contact_benchmark"

web:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) && cmake --build $(BUILD_DIR) --target reflect
	emcmake cmake -S . -B "$(WEB_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release -DCHIBI_REFLECT_EXECUTABLE=$(CURDIR)/$(BUILD_DIR)/reflect
	cmake --build "$(WEB_BUILD_DIR)" --target chibi check-behavior-backend

web-serve: web
	emrun --no_browser --port 8099 "$(WEB_BUILD_DIR)/chibi.html"

format:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) && cmake --build $(BUILD_DIR) --target format

check-format:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) && cmake --build $(BUILD_DIR) --target check-format

test:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --target check-generation check-reflect check-reload check-dev check-mcp check-behavior-backend
	cmake --build $(BUILD_DIR) --target test_suite
	env $(NATIVE_RUNTIME_ENV) $(BUILD_DIR)/test_suite
	env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --scene-documents
	env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --whole-function-audit
	cmake --build $(BUILD_DIR) --target chibi
	env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/chibi --headless --frames 3

clean:
	@if git worktree list --porcelain | awk -v root="$(abspath $(BUILD_DIR))" \
		'$$1 == "worktree" && ($$2 == root || index($$2, root "/") == 1) { found = 1 } END { exit !found }'; then \
		printf '%s\n' "Skipping $(BUILD_DIR): it contains a registered worktree"; \
	else \
		rm -rf "$(BUILD_DIR)"; \
	fi
	rm -rf "$(DEBUG_BUILD_DIR)" "$(RELEASE_BUILD_DIR)" "$(BENCHMARK_BUILD_DIR)" "$(TRACY_BUILD_DIR)" "$(TRACY_MCP_BUILD_DIR)" "$(WEB_BUILD_DIR)" "$(DEV_BUILD_DIR)"

# Watch sources and preserve the workspace across native application changes.
.PHONY: dev
DEV_BUILD_DIR ?= build-dev
JOBS ?= 4
DEV_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON

dev:
	cmake -S . -B "$(DEV_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(DEV_CMAKE_FLAGS) -DCHIBI_DEVELOPMENT_RELOAD=ON
	cmake --build "$(DEV_BUILD_DIR)" --target chibi chibi_dll --parallel $(JOBS)
	cmake -DDEV_CONFIG="$(abspath $(DEV_BUILD_DIR))/dev.json" -DRESULT="$(abspath $(DEV_BUILD_DIR))/dev-result.json" -DSKIP_BUILD=ON -DJOBS=$(JOBS) -P cmake/dev_build.cmake
	env $(NATIVE_RUNTIME_ENV) "$(DEV_BUILD_DIR)/chibi" --bootstrap "$(abspath $(DEV_BUILD_DIR))/dev-result.json" $(ARGS)
