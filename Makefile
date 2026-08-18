# Developer shortcuts. Override PYTHON, CMAKE, or BUILD_JOBS as needed:
#   make PYTHON=python3.12 BUILD_JOBS=8 wheel-check

PYTHON := python3
PIP    := pip3
CMAKE ?= cmake
BUILD_JOBS ?=

DEBUG_BUILD := build/debug
RELEASE_BUILD := build/release
DIST_DIR := dist
PY_CACHE := python/img_bat/__pycache__
BIN_NAME := bat_img
TEST_CACHE := tests/__pycache__

.DEFAULT_GOAL := help
.PHONY: help clean build-debug test build-release wheel wheel-check

help:
	@printf '%s\n' \
	  'make clean    Remove generated build and distribution files' \
	  'make debug    Configure and build a Debug executable' \
	  'make test     Build Debug and run CTest' \
	  'make release  Configure and build a Release executable' \
	  'make wheel    Build a release Python wheel in dist/' \
	  'make check    Build, install, and smoke-test the wheel in a temporary venv'

clean:
	$(CMAKE) -E rm -rf build $(DIST_DIR) $(PY_CACHE) $(TEST_CACHE)

debug:
	$(CMAKE) -S . -B $(DEBUG_BUILD) -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
	$(CMAKE) --build $(DEBUG_BUILD) --parallel $(BUILD_JOBS)

test: debug
	ctest --test-dir $(DEBUG_BUILD) --output-on-failure

release:
	$(CMAKE) -S . -B $(RELEASE_BUILD) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
	$(CMAKE) --build $(RELEASE_BUILD) --parallel $(BUILD_JOBS)

wheel: release
	$(PYTHON) -m pip wheel --no-deps --wheel-dir $(DIST_DIR) .

check: wheel
	@wheel="$$(find $(DIST_DIR) -maxdepth 1 -type f -name '*.whl' -print -quit)"; \
	if [ -z "$$wheel" ]; then echo 'No wheel was produced.' >&2; exit 1; fi; \
	venv="$$(mktemp -d)"; \
	trap 'rm -rf "$$venv"' EXIT; \
	$(PYTHON) -m venv "$$venv"; \
	"$$venv/bin/python" -m pip install --no-deps "$$wheel"; \
	"$$venv/bin/img_bat" --help >/dev/null; \
	"$$venv/bin/python" -c 'import img_bat; assert img_bat._binary_path()'; \
	echo "==> Running twine check …"; \
	"$$venv/bin/python" -m pip install twine; \
	"$$venv/bin/python" -m twine check $(DIST_DIR)/*; \
	echo ""; \
	echo "==> Checking wheel contents …"; \
	"$$venv/bin/python" -m zipfile -l $(DIST_DIR)/*.whl; \
	echo ""; \
	echo "==> Fore-reinstall local wheel …"; \
	"$$venv/bin/python" -m pip install --force-reinstall $(DIST_DIR)/*.whl; \
	echo ""; \
	echo "==> Smoke test ($(BIN_NAME) --version) …"; \
	"$$venv/bin/img_bat" --version; \
	echo ""; \
	echo "==> Smoke test ($(BIN_NAME) --help) …"; \
	"$$venv/bin/img_bat" --help
