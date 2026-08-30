# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR := build
BENCHMARK_BUILD_DIR ?= build-release

.PHONY: all build run benchmark web web-serve format check-format test spec-check clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR)

run: build
	$(BUILD_DIR)/app $(ARGS)

benchmark:
	cmake -S . -B $(BENCHMARK_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BENCHMARK_BUILD_DIR) --target bench_activation scene_benchmark system_benchmark query_benchmark
	$(BENCHMARK_BUILD_DIR)/bench_activation $(ARGS)
	$(BENCHMARK_BUILD_DIR)/scene_benchmark
	$(BENCHMARK_BUILD_DIR)/system_benchmark $(ARGS)
	$(BENCHMARK_BUILD_DIR)/query_benchmark

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

test: build spec-check
	$(BUILD_DIR)/self_check
	$(BUILD_DIR)/editor_runtime_check
	$(BUILD_DIR)/prototype_authoring_check
	$(BUILD_DIR)/input_behavior_check
	$(BUILD_DIR)/movement_system_check
	$(BUILD_DIR)/integration
	$(BUILD_DIR)/registry_consumer

# Every docs/spec.md requirement is claimed by a roadmap phase, and every
# requirement in a completed phase is cited by a check. See docs/adr/0007.
spec-check:
	python3 tools/spec_coverage.py

clean:
	rm -rf $(BUILD_DIR) $(BENCHMARK_BUILD_DIR)
