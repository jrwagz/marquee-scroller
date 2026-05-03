IS_TTY:=$(shell tty -s && echo 1 || echo 0)
ifeq ($(IS_TTY), 1)
DOCKER_TTY_ARGS=-t
else
DOCKER_TTY_ARGS=
endif

MD_FILES:=$(shell find . -name "*.md" -not -path "./.venv/*" -not -path "*/.venv/*" -not -path "./.pytest_cache/*" -not -path "*/.pytest_cache/*" -not -path "./lib/*" -not -path "./.pio/*" -not -path "./.platformio/*" -not -path "*/node_modules/*")

MARKDOWNLINT_IMAGE:=davidanson/markdownlint-cli2:v0.22.0
PIO_IMAGE:=ghcr.io/jrwagz/pio-image:v6.1.19
PYTEST_IMAGE:=marquee-pytest:local
RUFF_IMAGE:=ghcr.io/astral-sh/ruff:0.15.12
NODE_IMAGE:=node:20-alpine
# python:3-alpine is small and gives us pip for `esptool merge_bin`. The
# pio-bundled esptool is v3.0 which predates merge_bin (added in 3.1).
ESPTOOL_IMAGE:=python:3-alpine
LOCAL_PIO_CACHE:=./.platformio
PIO_CACHE:=$(HOME)/.platformio

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  help              - Show this help message"
	@echo "  lint-markdown     - Run markdownlint on all Markdown files"
	@echo "  lint-markdown-fix - Auto-fix markdownlint issues"
	@echo "  lint-python       - Run ruff on scripts/ and tests/scripts/"
	@echo "  lint              - Run lint-markdown + lint-python"
	@echo "  test-native       - Run native C++ unit tests (no device required)"
	@echo "  test-scripts      - Run Python tests for scripts/ with 100% coverage check"
	@echo "  test-integration  - Run integration tests against a live device (requires HOST=<ip>)"
	@echo "  test              - Run test-native + test-scripts"
	@echo "  webui             - Build the SPA frontend (Vite/Preact) into data/spa/"
	@echo "  webui-typecheck   - TypeScript-typecheck the SPA without emitting"
	@echo "  webui-clean       - Remove SPA build output and node_modules"
	@echo "  uploadfs          - Build webui + serial-flash data/ to device LittleFS (wipes /conf.txt)"
	@echo "  merged            - Combine firmware.bin + littlefs.bin → artifacts/merged.bin (first-install image)"
	@echo "  ready             - Full pipeline"

.passwd:
	echo "${USER}:x:$(shell id -u):$(shell id -g)::${HOME}:/bin/bash" > .passwd


.PHONY: clean-pio
clean-pio:
	rm -rf .pio/

.PHONY: clean-passwd
clean-passwd:
	rm -rf .passwd

.PHONY: clean-artifacts
clean-artifacts:
	rm -rf artifacts/

.PHONY: clean-pytest-image
clean-pytest-image:
	rm -f .pytest-image
	docker rmi $(PYTEST_IMAGE) 2>/dev/null || true

.PHONY: clean
clean: clean-pio clean-passwd clean-artifacts clean-pytest-image webui-clean

.PHONY: lint-markdown
lint-markdown: .passwd
	docker run \
		--rm ${DOCKER_TTY_ARGS} \
		-v ${PWD}:${PWD}:ro \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		$(MARKDOWNLINT_IMAGE) \
		--config .markdownlint.yaml ${MD_FILES}

.PHONY: lint-markdown-fix
lint-markdown-fix: .passwd
	docker run \
		--rm ${DOCKER_TTY_ARGS} \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		$(MARKDOWNLINT_IMAGE) \
		--fix --config .markdownlint.yaml ${MD_FILES}

.PHONY: lint-python
lint-python: .passwd
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		$(RUFF_IMAGE) \
		check scripts/ tests/scripts/

.PHONY: lint
lint: lint-markdown lint-python

.pytest-image: Dockerfile.pytest
	docker build -t $(PYTEST_IMAGE) -f Dockerfile.pytest .
	touch .pytest-image

.PHONY: test-scripts
test-scripts: .passwd .pytest-image
	mkdir -p artifacts
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		$(PYTEST_IMAGE) \
		pytest tests/scripts/ \
			--cov=scripts --cov-report=term-missing --cov-fail-under=90 \
			--junit-xml=artifacts/test-scripts-results.xml

.PHONY: test
test: test-native test-scripts

.PHONY: test-native
test-native: .passwd
	mkdir -p $(LOCAL_PIO_CACHE) artifacts
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-v $(LOCAL_PIO_CACHE):$(PIO_CACHE) \
		-u $(shell id -u):$(shell id -g) \
		$(PIO_IMAGE) \
		pio test -e native_test --junit-output-path artifacts/test-native-results.xml

.PHONY: test-integration
test-integration:
ifndef HOST
	$(error HOST is required. Usage: make test-integration HOST=192.168.1.100 [PASSWORD=mypass])
endif
	pytest tests/integration/ -v --host $(HOST) $(if $(PASSWORD),--password $(PASSWORD),) $(if $(PORT),--port $(PORT),)

.PHONY: build
build: .passwd
	mkdir -p $(LOCAL_PIO_CACHE) artifacts
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-e CI \
		-e USER \
		-e GIT_HASH=$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-v $(LOCAL_PIO_CACHE):$(PIO_CACHE) \
		-u $(shell id -u):$(shell id -g) \
		$(PIO_IMAGE) \
		pio run -e default
	@printf '\nBuilt: %s\n\n' "$$(cat artifacts/VERSION.txt)"

.PHONY: buildfs
buildfs: .passwd
	mkdir -p $(LOCAL_PIO_CACHE)
	python3 scripts/write_spa_version.py
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-e CI \
		-e USER \
		-e GIT_HASH=$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-v $(LOCAL_PIO_CACHE):$(PIO_CACHE) \
		-u $(shell id -u):$(shell id -g) \
		$(PIO_IMAGE) \
		pio run -e default --target buildfs

# Serial-flash data/ to the device's LittleFS partition. Wipes the entire FS
# (including /conf.txt) — see docs/WEBUI.md "Option A". Builds the SPA bundle
# first so a stale data/spa/ doesn't get flashed. Pass UPLOAD_PORT=/dev/cu.X
# to override port autodetection. Runs against the host's pio (esptool needs
# real USB device access, which doesn't work cleanly through Docker on macOS).
.PHONY: uploadfs
uploadfs: webui
	pio run -e default --target uploadfs $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT))

.PHONY: artifacts
artifacts:
	mkdir -p artifacts/
	cp .pio/build/default/firmware.bin artifacts/firmware.bin
	@if [ -f .pio/build/default/littlefs.bin ]; then \
		cp .pio/build/default/littlefs.bin artifacts/littlefs.bin; \
		echo "Included littlefs.bin ($$(stat -f%z artifacts/littlefs.bin 2>/dev/null || stat -c%s artifacts/littlefs.bin) bytes)"; \
	fi
	@if [ -d data/spa ]; then \
		mkdir -p artifacts/spa && cp -R data/spa/. artifacts/spa/; \
		echo "Included SPA bundle"; \
	fi

# merged.bin = firmware.bin (at 0x0) + littlefs.bin (at 0x300000) joined
# into a single image via `esptool merge_bin`. This is the recommended
# first-time install path: one esptool command, no offset arithmetic, no
# risk of mismatched firmware/SPA versions on a fresh device.
#
# OTA updates still use firmware.bin alone (sketch only) and SPA refreshes
# still use littlefs.bin alone — the merged image is for fresh installs
# only because it wipes /conf.txt. See docs/WEBUI.md and README.md.
#
# 0x300000 is the LittleFS partition start for d1_mini with the 4MB
# FS:1MB OTA:~1019KB layout used by this project (see platformio.ini).
#
# This is the single source of truth for the merge step — both `make merged`
# and the CI release workflow target this rule, so the esptool invocation
# only exists in one place.
artifacts/merged.bin: artifacts/firmware.bin artifacts/littlefs.bin .passwd
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		-e HOME=/tmp \
		$(ESPTOOL_IMAGE) \
		sh -c "pip install --quiet --no-cache-dir --target=/tmp/pylib 'esptool>=4.0' && \
		       PYTHONPATH=/tmp/pylib python /tmp/pylib/bin/esptool.py --chip esp8266 merge_bin \
		         -o artifacts/merged.bin \
		         --flash_mode dio --flash_size 4MB \
		         0x0 artifacts/firmware.bin \
		         0x300000 artifacts/littlefs.bin"
	@printf '\nmerged.bin: %s bytes\n' "$$(stat -f%z artifacts/merged.bin 2>/dev/null || stat -c%s artifacts/merged.bin)"

# Full pipeline: build sketch, build FS, gather artifacts, merge. Use this
# from a fresh checkout. From CI (where build/buildfs/artifacts already
# ran), invoke `make artifacts/merged.bin` directly to skip the rebuild
# check on the upstream targets.
.PHONY: merged
merged: build buildfs artifacts artifacts/merged.bin

# webui/ — Preact SPA built with Vite. Output lands in data/spa/ alongside
# .gz siblings so AsyncWebServer's serveStatic handler can ship gzipped
# bytes directly (saves ~3-4x on flash bandwidth + LittleFS storage).
#
# The npm install step is gated on a stamp file so we don't reinstall on
# every build. If webui/package.json changes, the stamp is invalidated
# and dependencies are reinstalled.
webui/node_modules/.installed: webui/package.json
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD}/webui \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		-e HOME=${PWD}/webui \
		-e npm_config_cache=${PWD}/webui/.npm-cache \
		$(NODE_IMAGE) \
		npm install --no-audit --no-fund
	touch webui/node_modules/.installed

.PHONY: webui
webui: .passwd webui/node_modules/.installed
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD}/webui \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		-e HOME=${PWD}/webui \
		-e npm_config_cache=${PWD}/webui/.npm-cache \
		$(NODE_IMAGE) \
		npm run build
	@echo ""
	@echo "SPA bundle written to data/spa/. Sizes:"
	@cd data/spa && find . -type f \( -name '*.js' -o -name '*.css' -o -name '*.html' \) -exec ls -l {} \; | awk '{printf "  %8d  %s\n", $$5, $$NF}'
	@echo ""
	@echo "  Total raw  : $$(find data/spa -type f -not -name '*.gz' -exec cat {} + | wc -c) bytes"
	@echo "  Total gzip : $$(find data/spa -name '*.gz' -exec cat {} + | wc -c) bytes"

.PHONY: webui-typecheck
webui-typecheck: .passwd webui/node_modules/.installed
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD}/webui \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-u $(shell id -u):$(shell id -g) \
		-e HOME=${PWD}/webui \
		-e npm_config_cache=${PWD}/webui/.npm-cache \
		$(NODE_IMAGE) \
		npm run typecheck

.PHONY: webui-clean
webui-clean:
	rm -rf data/spa webui/node_modules webui/.vite

.PHONY: ready
ready: clean lint test build artifacts webui webui-typecheck
