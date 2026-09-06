# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR := build
BENCHMARK_BUILD_DIR ?= build-release
LOCAL_CMAKE_FLAGS ?= -DCBLUEPRINT_ENABLE_SANITIZERS=ON -DCBLUEPRINT_ENABLE_WARNINGS=ON -DCBLUEPRINT_ENABLE_CCACHE=ON

.PHONY: all build run benchmark web web-serve format check-format test ui-integration clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",) && cmake --build $(BUILD_DIR)

run: build
	$(BUILD_DIR)/app $(ARGS)

benchmark:
	cmake -S . -B $(BENCHMARK_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(LOCAL_CMAKE_FLAGS)
	cmake --build $(BENCHMARK_BUILD_DIR) --target bench_activation scene_benchmark system_benchmark query_benchmark contact_benchmark
	$(BENCHMARK_BUILD_DIR)/bench_activation $(ARGS)
	$(BENCHMARK_BUILD_DIR)/scene_benchmark
	$(BENCHMARK_BUILD_DIR)/system_benchmark $(ARGS)
	$(BENCHMARK_BUILD_DIR)/query_benchmark
	$(BENCHMARK_BUILD_DIR)/contact_benchmark

web:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target reflect
	emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DCBLUEPRINT_REFLECT_EXECUTABLE=$(CURDIR)/$(BUILD_DIR)/reflect
	cmake --build build-web --target app

web-serve: web
	emrun --no_browser --port 8099 build-web/app.html

format:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target format

check-format:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target check-format

test:
	cmake -S . -B $(BUILD_DIR) $(LOCAL_CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --target test_suite
	$(BUILD_DIR)/test_suite
	cmake --build $(BUILD_DIR) --target app
	$(BUILD_DIR)/app --headless --scenario resize-pane

ui-integration: test

clean:
	rm -rf $(BUILD_DIR) $(BENCHMARK_BUILD_DIR)
