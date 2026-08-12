# Developer shortcuts. Override PYTHON, CMAKE, or BUILD_JOBS as needed:
#   make PYTHON=python3.12 BUILD_JOBS=8 wheel-check

PYTHON ?= python3
CMAKE ?= cmake
BUILD_JOBS ?=

DEBUG_BUILD := build/debug
RELEASE_BUILD := build/release
DIST_DIR := dist

.DEFAULT_GOAL := help
.PHONY: help clean build-debug test build-release wheel wheel-check

help:
	@printf '%s\n' \
	  'make clean          Remove generated build and distribution files' \
	  'make build-debug    Configure and build a Debug executable' \
	  'make test           Build Debug and run CTest' \
	  'make build-release  Configure and build a Release executable' \
	  'make wheel          Build a release Python wheel in dist/' \
	  'make wheel-check    Build, install, and smoke-test the wheel in a temporary venv'

clean:
	$(CMAKE) -E rm -rf build $(DIST_DIR)

build-debug:
	$(CMAKE) -S . -B $(DEBUG_BUILD) -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
	$(CMAKE) --build $(DEBUG_BUILD) --parallel $(BUILD_JOBS)

test: build-debug
	ctest --test-dir $(DEBUG_BUILD) --output-on-failure

build-release:
	$(CMAKE) -S . -B $(RELEASE_BUILD) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	$(CMAKE) --build $(RELEASE_BUILD) --parallel $(BUILD_JOBS)

wheel:
	$(PYTHON) -m pip wheel --no-deps --wheel-dir $(DIST_DIR) .

wheel-check: wheel
	@wheel="$$(find $(DIST_DIR) -maxdepth 1 -type f -name '*.whl' -print -quit)"; \
	if [ -z "$$wheel" ]; then echo 'No wheel was produced.' >&2; exit 1; fi; \
	venv="$$(mktemp -d)"; \
	trap 'rm -rf "$$venv"' EXIT; \
	$(PYTHON) -m venv "$$venv"; \
	"$$venv/bin/python" -m pip install --no-deps "$$wheel"; \
	"$$venv/bin/bat_img" --help >/dev/null; \
	"$$venv/bin/python" -c 'import bat_img_cpp; assert bat_img_cpp._binary_path()'
