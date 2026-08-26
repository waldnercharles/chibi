# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR := build

.PHONY: all build run format check-format test spec-check clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR)

run: build
	$(BUILD_DIR)/app $(ARGS)

format:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target format

check-format:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target check-format

test: build spec-check
	$(BUILD_DIR)/self_check
	$(BUILD_DIR)/editor_runtime_check
	$(BUILD_DIR)/prototype_authoring_check
	$(BUILD_DIR)/input_behavior_check
	$(BUILD_DIR)/integration
	$(BUILD_DIR)/registry_consumer

# Every docs/spec.md requirement is claimed by a roadmap phase, and every
# requirement in a completed phase is cited by a check. See docs/adr/0007.
spec-check:
	python3 tools/spec_coverage.py

clean:
	rm -rf $(BUILD_DIR)
