SOURCES := $(shell find libs apps tests -name '*.hpp' -o -name '*.cpp')

.PHONY: help configure build test run ci sanitize format format-check lint clean

help:
	@echo "Targets:"
	@echo "  configure     Configure the debug preset"
	@echo "  build         Build the debug preset (configures first if needed)"
	@echo "  test          Run all tests (debug preset)"
	@echo "  run           Run sim-cli (debug preset); pass ARGS=\"--seed 42 --replay-out /tmp/replay.json\""
	@echo "  ci            Run the same checks CI runs (warnings-as-errors build + tests + format check + lint)"
	@echo "  sanitize      Build and test with ASan/UBSan (GCC/Clang only)"
	@echo "  format        Auto-format all sources with clang-format"
	@echo "  format-check  Check formatting without modifying files"
	@echo "  lint          Run clang-tidy (incl. camelCase/PascalCase naming rule); needs a configured build dir"
	@echo "  clean         Remove all build directories"

configure:
	cmake --preset debug

build: configure
	cmake --build --preset debug

test: build
	ctest --preset debug --output-on-failure

run: build
	./build/debug/apps/sim-cli/sim-cli $(ARGS)

ci:
	cmake --preset ci
	cmake --build --preset ci
	ctest --preset ci
	$(MAKE) format-check
	$(MAKE) lint BUILD_DIR=build/ci

sanitize:
	cmake --preset sanitize
	cmake --build --preset sanitize
	ctest --preset sanitize --output-on-failure

format:
	clang-format -i $(SOURCES)

format-check:
	clang-format --dry-run --Werror $(SOURCES)

BUILD_DIR ?= build/debug

# Needs a configured build dir with compile_commands.json (e.g. via `build` or `ci`).
lint:
	run-clang-tidy -p $(BUILD_DIR) $(SOURCES)

clean:
	rm -rf build
