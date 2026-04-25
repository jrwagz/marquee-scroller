IS_TTY:=$(shell tty -s && echo 1 || echo 0)
ifeq ($(IS_TTY), 1)
DOCKER_TTY_ARGS=-t
else
DOCKER_TTY_ARGS=
endif

MD_FILES:=$(shell find . -name "*.md" -not -path "./.venv/*" -not -path "./.pytest_cache/*" -not -path "./lib/*" -not -path "./.pio/*")

MARKDOWNLINT_IMAGE:=davidanson/markdownlint-cli2:v0.22.0

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  help              - Show this help message"
	@echo "  lint-markdown     - Run markdownlint on all Markdown files"
	@echo "  lint-markdown-fix - Auto-fix markdownlint issues"
	@echo "  lint              - Run lint-markdown"
	@echo "  ready             - Full pipeline"

.passwd:
	echo "${USER}:x:$(shell id -u):$(shell id -g)::${HOME}:/bin/bash" > .passwd

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
test:
	@echo "No tests written yet!"

.PHONY: build
build:
	@echo "No build implemented yet!"

.PHONY: artifacts
artifacts:
	@echo "No artifacts implemented yet!"
	mkdir -p artifacts/
	touch artifacts/empty.txt

.PHONY: ready
ready: lint test build artifacts
