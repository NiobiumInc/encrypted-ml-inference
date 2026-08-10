# Copyright 2023-present Niobium Microsystems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
# ML Inference FHE Benchmark - Makefile
# ==============================================================================
# Convenience shortcuts for the FHETCH-transport (SDK) build and the Python
# harness. The server compute runs on Niobium hardware over the public
# niobium-client transport (in-process simulator / FUNC_SIM / FPGA / Fog).
# The client SDK bundles its own OpenFHE + libnbfhetch, so there is no separate
# OpenFHE setup step — `make build-sdk` builds everything.

# ==============================================================================
# Platform / Directory Configuration
# ==============================================================================
UNAME_S := $(shell uname -s)
JOBS := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

ROOT_DIR := $(shell pwd)
BUILD_DIR := $(ROOT_DIR)/build
HARNESS_DIR := $(ROOT_DIR)/harness
SCRIPTS_DIR := $(ROOT_DIR)/scripts
VENV_DIR := $(ROOT_DIR)/bmenv

PYTHON := python3
VENV_ACTIVATE := . $(VENV_DIR)/bin/activate

# ==============================================================================
# PHONY Target Declarations
# ==============================================================================
.PHONY: help setup setup-venv build-sdk run-transport
.PHONY: clean clean-build clean-venv clean-runs clean-all distclean
.PHONY: check-venv info

.DEFAULT_GOAL := help

# ==============================================================================
# Help Target (Self-Documenting)
# ==============================================================================
help: ## Display this help message
	@echo ""
	@echo "ML Inference FHE Benchmark - Available Targets"
	@echo "=============================================="
	@echo ""
	@awk 'BEGIN {FS = ":.*##"; printf "Usage:\n  make \033[36m<target>\033[0m\n\n"} \
		/^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5) } \
		/^[a-zA-Z_-]+:.*?##/ { printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2 } \
		END { printf "\n" }' $(MAKEFILE_LIST)

##@ Setup Targets

setup: setup-venv ## Complete project setup (Python venv)
	@echo "✅ Setup complete. Run 'make build-sdk' to build the SDK/transport."

setup-venv: ## Setup Python virtual environment
	@if [ ! -d "$(VENV_DIR)" ]; then \
		echo "Creating Python virtual environment..." && \
		$(PYTHON) -m venv $(VENV_DIR) && \
		echo "Installing Python dependencies..." && \
		$(VENV_ACTIVATE) && $(PYTHON) -m pip install --upgrade pip && \
		$(VENV_ACTIVATE) && $(PYTHON) -m pip install --retries 5 -r requirements.txt && \
		echo "✅ Python environment setup complete."; \
	else \
		echo "✅ Python environment already exists at $(VENV_DIR)"; \
	fi

##@ FHETCH Transport (SDK / Fog)

build-sdk: ## Self-contained SDK/transport build (builds niobium-client + the ML stages)
	bash $(SCRIPTS_DIR)/build_task.sh

run-transport: ## Record+replay a profile. Vars: PROFILE, TARGET (local|FOG), OPT, BATCHES
	$(PYTHON) $(HARNESS_DIR)/run_submission.py \
		--profile $(or $(PROFILE),single) \
		--target $(or $(TARGET),local) \
		-O $(or $(OPT),O3) \
		$(if $(BATCHES),--batches $(BATCHES),)

##@ Cleaning Targets

clean: clean-build ## Remove build artifacts only
	@echo "✅ Build artifacts cleaned."

clean-build: ## Remove the SDK build directory
	@echo "Removing build artifacts..."
	rm -rf $(BUILD_DIR)

clean-venv: ## Remove Python virtual environment
	@echo "Removing Python virtual environment..."
	rm -rf $(VENV_DIR)

clean-runs: ## Remove runtime data (io, measurements, compute_wl_*)
	@echo "Removing runtime data..."
	rm -rf $(ROOT_DIR)/io
	rm -rf $(ROOT_DIR)/measurements
	rm -rf $(ROOT_DIR)/compute_wl_*
	rm -rf $(ROOT_DIR)/_wl_*
	rm -rf $(ROOT_DIR)/global_key_cache_*

clean-all: clean-build clean-runs ## Deep clean (build + runtime data)
	@echo "✅ Deep clean complete."

distclean: ## Git-aware clean: restore to fresh clone state
	@echo "⚠️  WARNING: This will reset all changes and clean all untracked files."
	@echo "Press Ctrl+C within 5 seconds to cancel..."
	@sleep 5
	git reset --hard
	git clean -fdx
	git submodule deinit -f --all
	git submodule update --init --recursive
	@echo "✅ Repository restored to fresh clone state."

##@ Information Targets

check-venv: ## Check if Python virtual environment exists
	@if [ ! -d "$(VENV_DIR)" ]; then \
		echo "❌ Python environment not found. Run 'make setup-venv'"; \
		exit 1; \
	fi

info: ## Display configuration information
	@echo ""
	@echo "ML Inference FHE Benchmark - Configuration"
	@echo "=========================================="
	@echo "Platform:              $(UNAME_S)"
	@echo "Parallel jobs:         $(JOBS)"
	@echo "Python:                $(PYTHON)"
	@echo ""
	@echo "Directories:"
	@echo "  Root:                $(ROOT_DIR)"
	@echo "  SDK build:           $(BUILD_DIR)"
	@echo "  Virtual env:         $(VENV_DIR)"
	@echo ""
	@echo "Status:"
	@if [ -d "$(VENV_DIR)" ]; then \
		echo "  Python venv:         ✅ Installed"; \
	else \
		echo "  Python venv:         ❌ Not found (run 'make setup-venv')"; \
	fi
	@if [ -f "$(BUILD_DIR)/server_encrypted_compute_sdk" ]; then \
		echo "  SDK build:           ✅ Built"; \
	else \
		echo "  SDK build:           ❌ Not built (run 'make build-sdk')"; \
	fi
	@echo ""
