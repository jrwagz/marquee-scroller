IS_TTY:=$(shell tty -s && echo 1 || echo 0)
ifeq ($(IS_TTY), 1)
DOCKER_TTY_ARGS=-t
else
DOCKER_TTY_ARGS=
endif

MD_FILES:=$(shell find . -name "*.md" -not -path "./.venv/*" -not -path "*/.venv/*" -not -path "./.pytest_cache/*" -not -path "*/.pytest_cache/*" -not -path "./lib/*" -not -path "./.pio/*" -not -path "./.platformio/*")

MARKDOWNLINT_IMAGE:=davidanson/markdownlint-cli2:v0.22.0
PIO_IMAGE:=ghcr.io/jrwagz/pio-image:v6.1.19
PYTEST_IMAGE:=marquee-pytest:local
RUFF_IMAGE:=ghcr.io/astral-sh/ruff:0.15.12
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
	@echo "  test-server       - Run server Python tests in Docker"
	@echo "  test-integration  - Run integration tests against a live device (requires HOST=<ip>)"
	@echo "  test              - Run test-native + test-scripts + test-server"
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

.PHONY: clean-pytest-image
clean-pytest-image:
	rm -f .pytest-image
	docker rmi $(PYTEST_IMAGE) 2>/dev/null || true

.PHONY: clean
clean: clean-pio clean-passwd clean-artifacts clean-pytest-image

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
			--cov=scripts --cov-report=term-missing --cov-fail-under=100 \
			--junit-xml=artifacts/test-scripts-results.xml

SERVER_IMAGE:=wagfam-server-test

.PHONY: test-server
test-server:
	docker build -t $(SERVER_IMAGE) server/
	docker run \
		--rm $(DOCKER_TTY_ARGS) \
		-e WAGFAM_WAGFAM_API_KEY=test-key \
		$(SERVER_IMAGE) \
		python -m pytest tests/ -v

.PHONY: test
test: test-native test-scripts test-server

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
		-e CI \
		-e USER \
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
