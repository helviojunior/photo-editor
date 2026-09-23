#!/bin/bash
# Incrementa a versao em 1. Cada numerador vai ate 999 antes de virar o de cima:
#
#   1.0.0 -> 1.0.1 -> ... -> 1.0.999 -> 1.1.0 -> ... -> 1.999.999 -> 2.0.0
#
# Nao e semver: o ultimo numero nao significa "correcao", significa "mais um
# commit". O que a versao responde e "qual commit esta rodando no deploy", e
# para isso um contador continuo serve melhor do que decidir, a cada commit, se
# aquilo foi feature ou fix.
#
# Rode ANTES de commitar e inclua o VERSION no mesmo commit: a versao tem de
# apontar para o commit que a carrega, nao para o anterior.
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILE="${DIR}/VERSION"
CURRENT="$(tr -d ' \n\r' < "$FILE")"

IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"

PATCH=$((PATCH + 1))
if [ "$PATCH" -gt 999 ]; then
    PATCH=0
    MINOR=$((MINOR + 1))
fi
if [ "$MINOR" -gt 999 ]; then
    MINOR=0
    MAJOR=$((MAJOR + 1))
fi

NEXT="${MAJOR}.${MINOR}.${PATCH}"
printf '%s\n' "$NEXT" > "$FILE"
echo "$CURRENT -> $NEXT"
