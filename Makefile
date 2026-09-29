# PhotoEditor — base de compilacao do projeto.
#
#   make                 lista os alvos (o mesmo que `make help`)
#   make run             abre o app desktop (runtime do host, dev)
#   make dist            instaladores de todas as plataformas (builder Docker)
#
# O trabalho pesado mora no tools/build.py (so biblioteca padrao); aqui ficam
# os atalhos e o builder em Docker (tools/Dockerfile), que monta os pacotes de
# TODAS as plataformas num container Linux. Compativel com o GNU Make 3.81 do
# macOS.

PYTHON      ?= python3
VERSION     := $(shell cat VERSION)
BUILDER     ?= photoeditor-builder
# Alvos do `make dist` (ver TARGETS em tools/build.py): all | macos-arm64 |
# windows-x64 | linux-x64 | linux-arm64 | macos-x64 — varios separados por espaco.
TARGETS     ?= all
# Argumentos extras para o app no `make run` (ex.: ARGS="--devtools ~/Fotos/evento").
ARGS        ?=
# Onde o `make art` grava a previa da arte dos instaladores.
ART_OUT     ?= .cache/art

# Container descartavel com o uid do host (os arquivos gerados sao seus) e o
# repositorio montado em /src; o ENTRYPOINT e o tools/build.py.
BUILDER_RUN  = docker run --rm --user "$$(id -u):$$(id -g)" -v "$(CURDIR)":/src $(BUILDER)
# Node sem instalar nada no host (regra 15.1 do CLAUDE.md).
NODE_RUN     = docker run --rm -v "$(CURDIR)/frontend":/app -w /app node:20-alpine
# Python embarcado do host (criado pelo `make runtime`).
HOST_PY      = $(shell $(PYTHON) -c "import sys; sys.path.insert(0, 'tools'); import build; print(build.python_exe(build.host_target()))")
# Pasta de projeto descartavel para os comandos do Django que exigem uma.
CHECK_PROJECT = .cache/check-project

target_args  = $(foreach t,$(TARGETS),--target $(t))

.PHONY: default help run dev runtime model frontend builder dist macos dmg windows msi \
        linux art test test-frontend test-backend migrations bump clean clean-all

default: help

help:			## Mostra esta ajuda
	@echo "PhotoEditor $(VERSION)"
	@echo
	@grep -E '^[a-zA-Z_-]+:.*## ' $(MAKEFILE_LIST) | sed -e 's/:[^#]*## */|/' \
		| awk -F'|' '{ printf "  make %-15s %s\n", $$1, $$2 }'
	@echo
	@echo "Variaveis: TARGETS=\"macos-arm64 windows-x64\"  ARGS=\"--devtools\"  PYTHON=python3"

# --------------------------------------------------------------------------
# Desenvolvimento (no host)
# --------------------------------------------------------------------------

run:			## Abre o app (baixa runtime/modelo e builda o frontend se faltar)
	$(PYTHON) tools/build.py run -- $(ARGS)

dev:			## Abre o app com DevTools (F12) e menu de contexto
	$(PYTHON) tools/build.py run -- --devtools $(ARGS)

runtime:		## Python embarcado + dependencias do host em .runtime/
	$(PYTHON) tools/build.py runtime

model:			## Baixa o modelo SAM 2.1 (camadas) em models/
	$(PYTHON) tools/build.py model

frontend: builder	## Build do React em frontend/build (no container)
	$(BUILDER_RUN) frontend --isolated

# --------------------------------------------------------------------------
# Instaladores (builder em Docker)
# --------------------------------------------------------------------------

builder:		## Cria/atualiza a imagem do builder (tools/Dockerfile)
	docker build -t $(BUILDER) -f tools/Dockerfile tools

dist: builder		## Instaladores de TARGETS (padrao: todos) em dist/
	$(BUILDER_RUN) dist $(call target_args)

macos: builder		## macOS: PhotoEditor.app num .dmg
	$(BUILDER_RUN) dist --target macos-arm64

dmg: macos		## Alias de `make macos`

windows: builder	## Windows: instalador .msi
	$(BUILDER_RUN) dist --target windows-x64

msi: windows		## Alias de `make windows`

linux: builder		## Linux x64 e arm64: .tar.gz
	$(BUILDER_RUN) dist --target linux-x64 --target linux-arm64

art: builder		## Previa da arte dos instaladores (icones, fundo do DMG) em ART_OUT
	@mkdir -p $(ART_OUT)
	docker run --rm --user "$$(id -u):$$(id -g)" -v "$(CURDIR)":/src -w /src \
		--entrypoint python $(BUILDER) -m tools.packaging.art $(ART_OUT)

# --------------------------------------------------------------------------
# Verificacao e manutencao
# --------------------------------------------------------------------------

test: test-backend test-frontend	## Todas as verificacoes

test-backend: runtime	## Django check + migrations em dia (runtime do host)
	@mkdir -p $(CHECK_PROJECT)
	cd backend && PROJECT_ROOT="$(CURDIR)/$(CHECK_PROJECT)" DATA_DIR="$(CURDIR)/.cache/check-data" \
		"$(HOST_PY)" manage.py check
	cd backend && PROJECT_ROOT="$(CURDIR)/$(CHECK_PROJECT)" DATA_DIR="$(CURDIR)/.cache/check-data" \
		"$(HOST_PY)" manage.py makemigrations --check --dry-run

test-frontend:		## Build com CI=true (warning quebra) + testes do React
	$(NODE_RUN) sh -c 'yarn install --frozen-lockfile --silent && CI=true GENERATE_SOURCEMAP=false yarn build && CI=true yarn test --watchAll=false'

migrations: runtime	## Gera a migration de mudancas nos modelos (regra 13)
	@mkdir -p $(CHECK_PROJECT)
	cd backend && PROJECT_ROOT="$(CURDIR)/$(CHECK_PROJECT)" DATA_DIR="$(CURDIR)/.cache/check-data" \
		"$(HOST_PY)" manage.py makemigrations photoeditor

bump:			## Sobe a versao em VERSION (antes de cada commit, regra 17)
	./bump-version.sh

clean:			## Apaga dist/, o build do React e os temporarios dos instaladores
	rm -rf dist frontend/build .cache/pkg .cache/frontend-work $(ART_OUT) $(CHECK_PROJECT) .cache/check-data

clean-all: clean	## clean + runtimes, downloads e modelo (tudo que se baixa de novo)
	rm -rf .runtime .runtime-builder .cache models
