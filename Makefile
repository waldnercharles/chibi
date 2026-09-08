# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR := build
BENCHMARK_BUILD_DIR ?= build-release
# Apple Clang 17 does not unregister ASan globals on dlclose. Use installed
# upstream LLVM for native sanitized reload tests, without disabling checks.
NATIVE_TOOLCHAIN_FLAGS ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++)
LOCAL_CMAKE_FLAGS ?= -DCBLUEPRINT_ENABLE_SANITIZERS=ON -DCBLUEPRINT_ENABLE_WARNINGS=ON -DCBLUEPRINT_ENABLE_CCACHE=ON
# AddressSanitizer reserves a large shadow-memory range on macOS. Disable
# Apple's nano allocator before starting instrumented native binaries so the
# allocator does not report a failed nano-zone reservation.
NATIVE_RUNTIME_ENV ?= MallocNanoZone=0

.PHONY: all build run benchmark web web-serve format check-format test ui-native render-checks review clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",) && cmake --build $(BUILD_DIR)

run: build
	env $(NATIVE_RUNTIME_ENV) $(BUILD_DIR)/app $(ARGS)

benchmark:
	cmake -S . -B $(BENCHMARK_BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) -DCMAKE_BUILD_TYPE=Release $(LOCAL_CMAKE_FLAGS)
	cmake --build $(BENCHMARK_BUILD_DIR) --target bench_activation scene_benchmark system_benchmark query_benchmark contact_benchmark
	env $(NATIVE_RUNTIME_ENV) $(BENCHMARK_BUILD_DIR)/bench_activation $(ARGS)
	env $(NATIVE_RUNTIME_ENV) $(BENCHMARK_BUILD_DIR)/scene_benchmark
	env $(NATIVE_RUNTIME_ENV) $(BENCHMARK_BUILD_DIR)/system_benchmark $(ARGS)
	env $(NATIVE_RUNTIME_ENV) $(BENCHMARK_BUILD_DIR)/query_benchmark
	env $(NATIVE_RUNTIME_ENV) $(BENCHMARK_BUILD_DIR)/contact_benchmark

web:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) && cmake --build $(BUILD_DIR) --target reflect
	emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DCBLUEPRINT_REFLECT_EXECUTABLE=$(CURDIR)/$(BUILD_DIR)/reflect
	cmake --build build-web --target app

web-serve: web
	emrun --no_browser --port 8099 build-web/app.html

format:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) && cmake --build $(BUILD_DIR) --target format

check-format:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) && cmake --build $(BUILD_DIR) --target check-format

test:
	cmake -S . -B $(BUILD_DIR) $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --target check-generation check-reflect check-reload check-dev
	cmake --build $(BUILD_DIR) --target test_suite
	env $(NATIVE_RUNTIME_ENV) $(BUILD_DIR)/test_suite
	cmake --build $(BUILD_DIR) --target app
	env $(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/app --headless --frames 3

ui-native: build
	$(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --ui-native

render-checks: build
	$(NATIVE_RUNTIME_ENV) ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/test_suite --render-checks

clean:
	rm -rf $(BUILD_DIR) $(BENCHMARK_BUILD_DIR)

# Watch sources and preserve the workspace across native application changes.
.PHONY: dev
DEV_BUILD_DIR ?= build-dev
JOBS ?= 4

dev:
	cmake -S . -B "$(DEV_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) -DCBLUEPRINT_DEVELOPMENT_RELOAD=ON
	cmake --build "$(DEV_BUILD_DIR)" --target blueprints_host blueprints_application --parallel $(JOBS)
	cmake -DDEV_CONFIG="$(abspath $(DEV_BUILD_DIR))/dev.json" -DRESULT="$(abspath $(DEV_BUILD_DIR))/dev-result.json" -DSKIP_BUILD=ON -DJOBS=$(JOBS) -P cmake/dev_build.cmake
	env $(NATIVE_RUNTIME_ENV) "$(DEV_BUILD_DIR)/blueprints_host" --bootstrap "$(abspath $(DEV_BUILD_DIR))/dev-result.json" $(ARGS)
