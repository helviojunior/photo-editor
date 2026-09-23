# TODO — PhotoEditor

Controle dos requisitos do editor de fotos de eventos (estilo Lightroom).
Executamos **um item por vez**, na ordem abaixo; cada item concluído é marcado
com `[x]` no mesmo commit que o implementa.

Legenda: `[ ]` pendente · `[~]` em andamento · `[x]` concluído

---

## Contexto: a pasta do projeto

Todo o backend roda em Docker. A pasta de trabalho do usuário é montada em
`/project` dentro do container:

```bash
-v ~/Storage/teste:/project
```

Layout esperado dentro de `/project`:

```
/project/
    raw/            fotos originais (somente leitura para o editor)
    project_data/   banco SQLite + caches gerados (thumbnails, previews)
    deleted/        fotos marcadas como excluídas (movidas, nunca apagadas)
    publicar/       saída do botão Exportar
```

---

## Decisões em aberto (resolver antes do item que depende delas)

- [ ] **SQLite × regra 12.1 do CLAUDE.md** (item 1): hoje a regra obriga
      PostgreSQL e proíbe sqlite. O requisito pede SQLite em
      `/project/project_data`. Proposta: o banco passa a ser o SQLite do
      projeto montado, e o serviço `postgres` sai dos compose. É preciso
      reescrever a regra 12.1 (e o trecho do `.env` único, regra 12).
- [ ] **Formato das fotos em `raw/`** (item 3): JPEG apenas, como no
      `../correct-photos`, ou também RAW de câmera (CR2/CR3/NEF/ARW)? RAW
      exige `rawpy`/LibRaw e muda o custo de preview.
- [ ] **Histórico do CTRL/CMD+Z** (item 6): vale só para a sessão aberta ou
      persiste no banco (sobrevive a recarregar a página)? Proposta: persistir.
- [ ] **Pasta de exportação**: `publicar/` (minúsculo, como pedido) ou
      `Publicar/` (como o `../correct-photos` gera)? Proposta: `publicar/`.

---

## 1. Infra e dados

- [ ] **1.1** Montar `/project` no container do backend (volume nos
      `docker-compose*.yml`, caminho local configurável no `.env`, ex.:
      `PROJECT_DIR=~/Storage/teste`).
- [ ] **1.2** Banco SQLite em `/project/project_data/db.sqlite3`, criado na
      primeira execução se não existir (depende da decisão SQLite × PostgreSQL).
- [ ] **1.3** No boot, o backend roda `makemigrations` + `migrate` para manter
      o banco atualizado (o `entrypoint.sh` já faz; validar com o SQLite).
- [ ] **1.4** Criar as subpastas `project_data/`, `deleted/` e `publicar/` se
      faltarem; `raw/` ausente gera erro claro no log, sem derrubar a aplicação.

## 2. Catálogo das fotos originais

- [ ] **2.1** Modelo `Photo`: nome do arquivo, caminho relativo, tamanho,
      dimensões, orientação EXIF, data/hora de captura
      (`DateTimeOriginal` + `SubsecTimeOriginal`, para ordenar como o evento
      aconteceu) e status (`active` / `deleted`).
- [ ] **2.2** Varredura de `/project/raw`: cataloga fotos novas, marca as que
      sumiram, não duplica as já conhecidas (idempotente). Roda no boot e por um
      endpoint "reescanear".
- [ ] **2.3** Derivados em cache em `project_data/` (thumbnail para a
      filmstrip e preview para o editor), gerados sob demanda, **sem nunca
      alterar o original**.
- [ ] **2.4** API: listar fotos (ordenadas pela captura), obter uma foto,
      servir thumbnail / preview / original.

## 3. Layout do editor (referência: `temp/IMG_5156.WEBP`)

- [ ] **3.1** Parte superior com **70% da altura** da tela:
    - à esquerda, a foto **original** (sem edição);
    - ao centro/direita, a foto **editada**;
    - na extrema direita, o **painel de controles de edição** (seção Edição).
- [ ] **3.2** A tela do editor tem rota própria por foto (ex.: `/photos/:id`),
      de modo que a navegação atualize a URL (regra 3 do CLAUDE.md).
- [ ] **3.3** Comportamento no celular/tablet (regra 2.4): definir como
      antes/depois e painel se reorganizam em tela estreita.

## 4. Filmstrip de thumbnails (referência: `temp/Xnip2026-09-23_15-33-04.png`)

- [ ] **4.1** Parte inferior (os 30% restantes) com a faixa de thumbnails.
- [ ] **4.2** A foto em edição fica **sempre ao centro** e destacada; à
      esquerda as anteriores, à direita as seguintes.
- [ ] **4.3** Clique em um thumbnail abre aquela foto no editor.
- [ ] **4.4** Contador no estilo "N fotos / posição atual".

## 5. Atalhos de teclado

- [ ] **5.1** `→` — próxima foto.
- [ ] **5.2** `←` — foto anterior.
- [ ] **5.3** `DEL` — marca a foto como excluída: **move** o arquivo de
      `raw/` para `deleted/` (nunca apaga) e avança para a próxima.
- [ ] **5.4** `CTRL+Z` / `CMD+Z` — desfaz a última ação (exclusão, ajuste,
      preset, auto…). Desfazer uma exclusão devolve o arquivo para `raw/`.
- [ ] **5.5** Modelo de histórico de ações (tipo, foto, estado anterior) que
      sustenta o desfazer (ver decisão sobre persistência).
- [ ] **5.6** Atalhos não disparam enquanto o foco está em um campo de texto
      ou slider.

## 6. Edição

As definições **não são gravadas na foto**: ficam no banco, numa relação da
foto com cada ajuste. A foto editada exibida é renderizada a partir do
original + ajustes.

- [ ] **6.1** Modelo de ajustes por foto (`Photo` → ajustes), com valores
      neutros por padrão e o preset aplicado, se houver.
- [ ] **6.2** Renderização do preview editado no backend a partir do
      original + ajustes (mesmo motor usado na exportação, para o que se vê
      ser o que se exporta).
- [ ] **6.3** **Auto** — portar o corretor `photofix_v2` do
      `../correct-photos` (`src/photofix/enhance.py` / `pipeline.py`):
      curva de tom que preserva o ponto branco e exposição medida no sujeito
      (`subject.py`). O resultado vira valores de ajuste editáveis, não um
      "carimbo" irreversível.
- [ ] **6.4** Ajustes manuais:
    - [ ] Saturação
    - [ ] Levels (Shadows, Highlights, Blacks, Whites, Exposure, Contrast…)
    - [ ] Outros controles a especificar
- [ ] **6.5** **Presets** — conjunto inspirado nos perfis mais comuns dos
      editores populares (Lightroom/VSCO: vívido, suave, P&B, quente, frio,
      vintage/matte…). Lista final a definir antes de implementar.
- [ ] **6.6** Botão para resetar a foto (volta ao neutro, desfazível).

## 7. Exportação

- [ ] **7.1** Botão **Exportar** que gera todas as fotos ativas (não
      excluídas) com seus ajustes em `/project/publicar/`.
- [ ] **7.2** Mesmo padrão de saída do `../correct-photos`
      (`src/photofix/publish.py`): JPEG numa caixa 1920×1080, 72 dpi,
      qualidade 88, `optimize` e `progressive`; **EXIF preservado**
      (`DateTimeOriginal`/`SubsecTimeOriginal`/`OffsetTimeOriginal`); pixels
      girados de vez com `Orientation = 1`; miniatura embutida removida.
- [ ] **7.3** Progresso visível durante a exportação (pode levar minutos) e
      resumo ao final, usando os modais do sistema (regra 1).
- [ ] **7.4** Reexportar sobrescreve apenas as fotos alteradas desde a última
      exportação (a definir).
