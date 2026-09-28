#!/bin/sh
# Monta os pacotes do app desktop dentro do container builder (tools/Dockerfile).
#
#   tools/docker-build.sh                              # todos os alvos
#   tools/docker-build.sh dist --target windows-x64    # um alvo
#   tools/docker-build.sh frontend --isolated          # qualquer comando do build.py
#
# Saida em dist/: PhotoEditor-<versao>-<alvo>/ e o .zip/.tar.gz correspondente.
set -e
cd "$(dirname "$0")/.."

if [ $# -eq 0 ]; then
    set -- dist --target all
fi

docker build -t photoeditor-builder -f tools/Dockerfile tools
exec docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src photoeditor-builder "$@"
