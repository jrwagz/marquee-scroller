.PHONY: run test docker-build docker-up docker-down install

install:
	pip install -e ".[dev]"

run:
	uvicorn app.main:app --reload

test:
	python -m pytest tests/ -v

docker-build:
	docker compose build

docker-up-local:
	docker compose --profile local up -d

docker-up-prod:
	docker compose --profile production up -d

docker-down:
	docker compose --profile local --profile production down
