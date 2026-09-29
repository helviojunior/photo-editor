# Builder dos pacotes do PhotoEditor desktop (Windows, macOS e Linux).
#
# Um container Linux so monta o pacote de TODAS as plataformas: nada do app e
# compilado. O Python de cada alvo e o CPython pronto do python-build-standalone
# e as dependencias sao wheels binarias baixadas com `pip --platform <alvo>`
# (ver tools/build.py). Os instaladores tambem saem daqui:
#
#   .dmg (macOS)  mkfs.hfsplus (hfsprogs) + libdmg-hfsplus, o mesmo caminho do
#                 Firefox; .DS_Store da janela com ds_store + mac_alias
#   .msi (Windows) wixl (msitools)
#
# Uso: pelo Makefile da raiz — `make dist`, `make macos`, `make windows`...

# Ubuntu 24.04 (tag datada = mesma base sempre): o unico com mkfs.hfsplus
# (hfsprogs saiu do Debian) e com Python 3.12, o mesmo do runtime embarcado —
# o que permite pre-compilar os .pyc do pacote aqui.
ARG BASE=ubuntu:noble-20260905

FROM node:20-bookworm-slim AS node

# libdmg-hfsplus (hfsplus + dmg): revisao fixa, o build sempre roda a mesma.
FROM ${BASE} AS libdmg
ARG LIBDMG_REVISION=ec239599c1f234a4e01ae3fe51214d0c77e5baa3
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl build-essential cmake zlib1g-dev libbz2-dev libssl-dev \
 && curl -fsSL "https://github.com/mozilla/libdmg-hfsplus/archive/${LIBDMG_REVISION}.tar.gz" \
      | tar -xz -C /tmp \
 && cmake -S /tmp/libdmg-hfsplus-${LIBDMG_REVISION} -B /tmp/libdmg-build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /tmp/libdmg-build --target dmg-bin hfsplus -j"$(nproc)" \
 && install -m 755 /tmp/libdmg-build/dmg/dmg /tmp/libdmg-build/hfs/hfsplus /usr/local/bin/

FROM ${BASE}

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates python3.12 python3.12-venv hfsprogs msitools wixl fonts-liberation2 \
        libbz2-1.0 zlib1g libssl3t64 \
 && rm -rf /var/lib/apt/lists/*
COPY --from=libdmg /usr/local/bin/dmg /usr/local/bin/hfsplus /usr/local/bin/

# Python do builder num venv (o do sistema e "externally managed"): pip
# recente para o `pip --platform` e as libs dos instaladores.
RUN python3.12 -m venv /opt/builder \
 && /opt/builder/bin/pip install --no-cache-dir --upgrade pip \
 && /opt/builder/bin/pip install --no-cache-dir "Pillow>=10" "ds_store==1.3.3" "mac_alias==2.2.3"
ENV PATH=/opt/builder/bin:$PATH

COPY --from=node /usr/local/bin/node /usr/local/bin/node
COPY --from=node /usr/local/lib/node_modules /usr/local/lib/node_modules
RUN ln -s ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm \
 && ln -s ../lib/node_modules/npm/bin/npx-cli.js /usr/local/bin/npx \
 && npm install -g yarn@1.22.22

# Roda com o uid do host (Makefile): HOME e caches em lugar gravavel,
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
