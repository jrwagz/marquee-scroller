IS_TTY:=$(shell tty -s && echo 1 || echo 0)
ifeq ($(IS_TTY), 1)
DOCKER_TTY_ARGS=-t
else
DOCKER_TTY_ARGS=
endif

MD_FILES:=$(shell find . -name "*.md" -not -path "./.venv/*" -not -path "*/.venv/*" -not -path "./.pytest_cache/*" -not -path "*/.pytest_cache/*" -not -path "./lib/*" -not -path "./.pio/*")

MARKDOWNLINT_IMAGE:=davidanson/markdownlint-cli2:v0.22.0
PIO_IMAGE:=ghcr.io/jrwagz/pio-image:v6.1.19
LOCAL_PIO_CACHE:=./.platformio
PIO_CACHE:=$(HOME)/.platformio

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  help              - Show this help message"
	@echo "  lint-markdown     - Run markdownlint on all Markdown files"
	@echo "  lint-markdown-fix - Auto-fix markdownlint issues"
	@echo "  lint              - Run lint-markdown"
	@echo "  test-native       - Run native C++ unit tests (no device required)"
	@echo "  test-integration  - Run integration tests against a live device (requires HOST=<ip>)"
	@echo "  ready             - Full pipeline"

.passwd:
	echo "${USER}:x:$(shell id -u):$(shell id -g)::${HOME}:/bin/bash" > .passwd


.PHONY: clean-pio
clean-pio:
	rm -rf .pio/
	rm -rf .platformio/

.PHONY: clean-passwd
clean-passwd:
	rm -rf .passwd

.PHONY: clean-artifacts
clean-artifacts:
	rm -rf artifacts/


.PHONY: clean
clean: clean-pio clean-passwd clean-artifacts

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

.PHONY: lint
lint: lint-markdown

.PHONY: test
test: test-native

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
	mkdir -p $(LOCAL_PIO_CACHE)
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-v ${PWD}:${PWD} \
		-w ${PWD} \
		-v ${PWD}/.passwd:/etc/passwd:ro \
		-v $(LOCAL_PIO_CACHE):$(PIO_CACHE) \
		-u $(shell id -u):$(shell id -g) \
		$(PIO_IMAGE) \
		pio run -e default

.PHONY: artifacts
artifacts:
	mkdir -p artifacts/
	cp .pio/build/default/firmware.bin artifacts/firmware.bin

.PHONY: ready
ready: clean lint test build artifacts
