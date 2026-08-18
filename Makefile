# Convenience wrapper around CMake. `make` configures and builds.
BUILD_DIR := build

.PHONY: all build run format check-format test clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR)

run: build
	$(BUILD_DIR)/app $(ARGS)

format:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target format

check-format:
	cmake -S . -B $(BUILD_DIR) && cmake --build $(BUILD_DIR) --target check-format

test: build
	$(BUILD_DIR)/self_check

clean:
	rm -rf $(BUILD_DIR)
