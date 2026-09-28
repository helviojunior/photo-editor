# Builder dos pacotes do PhotoEditor desktop (Windows, macOS e Linux).
#
# Um container Linux so monta o pacote de TODAS as plataformas: nada e
# compilado. O Python de cada alvo e o CPython pronto do python-build-standalone
# e as dependencias sao wheels binarias baixadas com `pip --platform <alvo>`
# (ver tools/build.py). Aqui dentro so precisa de Python (para o pip e o
# build.py) e de Node + yarn (para o build do React).
#
# Uso: tools/docker-build.sh [comando do build.py]  (padrao: dist --target all)

FROM node:20-bookworm-slim AS node

FROM python:3.12-slim-bookworm

COPY --from=node /usr/local/bin/node /usr/local/bin/node
COPY --from=node /usr/local/lib/node_modules /usr/local/lib/node_modules
RUN ln -s ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm \
 && ln -s ../lib/node_modules/npm/bin/npx-cli.js /usr/local/bin/npx \
 && npm install -g yarn@1.22.22 \
 && pip install --no-cache-dir --upgrade pip

# Roda com o uid do host (docker-build.sh): HOME e caches em lugar gravavel,
# caches dentro do repositorio (.cache/, ignorado pelo git) para reaproveitar
# downloads entre builds.
ENV PHOTOEDITOR_BUILDER=1 \
    HOME=/tmp \
    PIP_CACHE_DIR=/src/.cache/pip \
    YARN_CACHE_FOLDER=/src/.cache/yarn \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /src
ENTRYPOINT ["python", "tools/build.py"]
CMD ["dist", "--target", "all"]
