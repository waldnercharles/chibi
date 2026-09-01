# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR := build
BENCHMARK_BUILD_DIR ?= build-release
RECORD_PATH ?= /tmp/c-blueprints-resize-pane.mp4
FRAMES ?= 48

.PHONY: all build run record-evidence record-resize-pane benchmark web web-serve format check-format test clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) -DLIBCLANG_ROOT="$(LIBCLANG_ROOT)" && cmake --build $(BUILD_DIR)

run: build
	$(BUILD_DIR)/app $(ARGS)

record-evidence: build
	case "$(SCENARIO)" in \
		resize-pane) $(BUILD_DIR)/app --headless --record $(RECORD_PATH) --scenario resize-pane --frames $(FRAMES) ;; \
		authoring) $(BUILD_DIR)/app --headless --record $(RECORD_PATH) --scenario authoring --frames $(FRAMES) ;; \
		*) echo "Unsupported evidence scenario: $(SCENARIO)" >&2; exit 2 ;; \
	esac

record-resize-pane: SCENARIO=resize-pane
record-resize-pane: record-evidence

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

test:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target test_suite
	$(BUILD_DIR)/test_suite

clean:
	rm -rf $(BUILD_DIR) $(BENCHMARK_BUILD_DIR)
