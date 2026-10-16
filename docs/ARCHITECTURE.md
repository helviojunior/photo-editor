# PhotoEditor — arquitetura

App desktop nativo para Windows, macOS e Linux (x64 e arm64): **C++20 + Qt 6
(Qt Quick/QML)**, OpenCV estático e ONNX Runtime. Um processo só — sem
navegador embarcado e sem servidor local. A única parte em Python é o
auxiliar do Instagram, um processo pequeno que o app chama só para falar com
o Instagram. As convenções obrigatórias estão no [`CLAUDE.md`](../CLAUDE.md).

A versão anterior (Django + React + Qt WebEngine, ~1,5 GB instalada) está na
branch `legacy`. Esta é um porte dela: mesma interface, mesma matemática,
mesmos arquivos em disco.

## Stack

| Peça | Versão | Para quê |
|---|---|---|
| Qt (Core, Gui, Quick, QuickControls2, Sql, Concurrent, Svg) | 6.8.3 | interface, SQLite, threads |
| OpenCV (estático, só core/imgproc/imgcodecs/features2d/calib3d/objdetect) | 4.12.0 | revelação, Auto, Merge (SIFT/RANSAC), JPEG |
| ONNX Runtime (pacote oficial) | 1.30.0 (1.23.2 no Mac Intel) | SAM 2.1 tiny em CPU |
| CPython (python-build-standalone) + `instagrapi` | 3.12 | só o auxiliar do Instagram |

## Camadas do código

```
src/imaging   OpenCV + QtCore, sem banco e sem interface
     │          Develop (motor), Crop, SettingsHash, Auto, Merge, Segment (SAM),
     │          Mask, Exif, ImageIO, Publish
src/core      projeto + catálogo SQLite, ações, histórico, merges,
     │          derivados (preview/thumbnail/render), Exporter, projetos recentes
src/app       main, controllers expostos ao QML, image providers, qml/
src/instagram auxiliar Python (processo separado, JSON por linha)
```

- **`imaging`** é um porte 1:1 de `backend/photoeditor/imaging/*.py`. Os
  testes comparam com referências geradas pelo código Python
  (`tests/data/*/gen_*.py`): pixels da revelação dentro de ±1, valores do Auto
  iguais, matriz do Merge a 0,000 px, máscara do SAM com IoU 1,0.
- **`core`** é o que eram os `services/` do Django. `pe::Project` abre a pasta,
  trava (`QLockFile`), migra o banco e faz as ações (ajustes, exclusão,
  cópia, capa, versão Instagram, merge, desfazer).
- **`app`**: `AppController` (singleton `App` no QML) com `EditorController`,
  `MergeController` e `InstagramController`. O QML só desenha e chama.

### Threads

O banco é só da thread principal. Trabalho pesado recebe os dados prontos e
roda em pool:

| Trabalho | Quem monta (thread principal) | Onde roda |
|---|---|---|
| "Editada" do editor | `RenderRequest` (estado do rascunho + merge) | provider assíncrono `image://photo/render/<chave>` |
| preview / thumbnail / máscara / recorte do merge / capa da Home | producer registrado no `ImageRegistry` | provider assíncrono |
| traço do pincel (SAM) | pontos + máscara atual | pool de 1 thread (`SelectionChain`: ordem garantida) |
| alinhamento do merge (SIFT) | fotos do merge | `QtConcurrent` → grava ao terminar |
| Exportar | `ExportJob` por foto | `Exporter` (2 workers) → `exported_hash` volta por sinal |

A chave das imagens carrega tudo de que o resultado depende: a URL só muda
quando a imagem muda, e o QML não pede de novo o que já tem.

## Projetos e dados

```
<projeto>/raw/            originais (somente JPEG, nunca alterados)
<projeto>/project_data/   db.sqlite3, cache/{preview,thumbnail,render,merge,merge-preview}/, masks/
<projeto>/deleted/        fotos excluídas (movidas, nunca apagadas)
<projeto>/publicar/       saída do Exportar; publicar/instagram/ = versões do feed; capa.jpg
~/.photoe/                photoe.db (Home), covers/, logs/, instagram.json
```

### Compatibilidade com a versão Python

Projeto aberto pela versão anterior abre aqui sem conversão e sem
reexportar:

- **Esquema:** as mesmas tabelas `photoeditor_*`. Os passos do esquema
  (`src/core/schema/0001..0012.sql`) são o SQL exato das 12 migrations Django
  (`manage.py sqlmigrate`). Banco legado começa do número de migrations que a
  `django_migrations` registra; daí em diante, `PRAGMA user_version`.
- **Hashes:** `settings_hash`, `exported_hash` e `align_version` são montados
  como o `json.dumps(sort_keys=True)` do Python, inclusive o `repr` dos floats
  (`imaging/SettingsHash.cpp`), e o `mtime` é o `st_mtime_ns` exato
  (`core/FileTime.cpp`).
- **Caches** com os mesmos nomes (`<uuid com hífens>-<mtime_ns>.jpg`).
- Prova: `tst_core::legacyCatalogOpensWithoutChanges`, com um banco criado
  e exportado pela versão Python.

Diferenças conhecidas: máscaras NOVAS ganham outra chave (o PNG do OpenCV não
é byte a byte o do Pillow; as antigas continuam lidas); a redução de imagem
usa `INTER_AREA` em vez do LANCZOS do Pillow (mesmo tamanho, pixels quase
iguais); datas EXIF sem fuso usam o fuso do sistema (antes, a setting
`TIME_ZONE`).

## Processamento de imagem

### Revelação

Ordem fixa, igual no preview e na exportação: crop na resolução cheia →
redução (exportação) → motor (balanço de branco → curva de tom na luminância
→ sombras locais com guided filter → cor) → camadas, cada uma revelada do
mesmo original e misturada pela máscara. `kEngineVersion` sobe quando o
resultado de um mesmo ajuste muda.

### Camadas: SAM 2.1

O **SAM 2.1 tiny** (Meta, Apache-2.0) em ONNX roda **localmente em CPU**.
O modelo (~155 MB) vai no pacote; o build o baixa com revisão e sha256 fixos
(`make model` → `.cache/models/`). Custo medido: ~3 s na primeira seleção de
uma foto (carga + encoder) e ~0,3 s por traço. Com **Detectar o objeto (IA)**
desligado, o pincel seleciona exatamente a área pintada.

### Merge

Cada foto é alinhada à base pelo fundo: CLAHE + SIFT (6.000 pontos) + razão de
Lowe 0,75 + `estimateAffinePartial2D` (RANSAC, refinado por LMEDS), com a área
selecionada fora da análise; o ganho de cor é medido no fundo. A base vira a
foto do merge: a "Editada" parte do composto e o Exportar grava
`<base>_merge.jpg`.

### Instagram

- **Versão Instagram:** cópia virtual (`copy_of` = dona do JPEG,
  `instagram_of` = origem), com o quadro preso a 4:5, 1:1 ou 1,91:1. Sai em
  `publicar/instagram/` com 1080 px de largura, qualidade 92.
- **Auxiliar** (`src/instagram/photoe_instagram.py`): o `InstagramController`
  o inicia com `python -I -E -s ... --data-dir ~/.photoe` e troca JSON por
  linha (comandos `status`, `login`, `code`, `cancel`, `logout`,
  `set_template`, `publish`; eventos `login`/`publish`). Detalhes em
  `src/instagram/README.md`. Exportar as versões, validar o feed e gravar o
  post (`photoeditor_instagrampost`) é do C++.

## Interface (QML)

`src/app/qml/`: `Theme.qml` (tokens claro/escuro, escala da marca),
`Dialogs.qml`/`Messages.qml` (singletons: modais e textos de erro/status),
`Keymap.qml` (singleton: atalhos de teclado, padrões + trocas salvas no
QSettings), `AppHeader.qml` (logo, busca de ações, desfazer/refazer,
Exportar), `ActivityRail.qml` (trilho lateral, no lugar da barra de menus: Projetos, Editor, Idioma,
Instagram, Sobre, atalhos, tema), `components/` (Button, Toggle,
SelectionCheck, ValueSlider, Splitter, Modal, FormError, CommandPalette...),
`pages/` (Home, Editor, Merge, Configurações do Instagram), `editor/`
(painéis, crop, pincel, filmstrip) e `dialogs/` (Exportar, Publicar, editor de
atalhos, fluxos de abrir/criar projeto). Ícones Lucide em `src/app/icons/`,
fonte Inter em `src/app/fonts/`. Traduções em `i18n/` (Qt Linguist).

## Compilação e pacotes: `make`

Modelo do `../clivcut`: o `Makefile` é a porta de entrada e cada plataforma
tem o seu builder Docker em `packaging/<plataforma>/`.

| Alvo | Comando | Saída |
|---|---|---|
| desenvolvimento | `make build`, `make test`, `make run` (no builder Linux se não há Qt no host) | `build/` |
| Linux x64/arm64 | `make linux` / `make linux-all` | `dist/*.tar.gz` |
| macOS x64/arm64 | `make macos-sdk` (uma vez) + `make dmg` / `make dmg-all` — cross-compile com LLVM e o SDK deste Mac | `dist/*.dmg` |
| Windows x64 | `make windows` — cross-compile llvm-mingw, `.msi` com wixl | `dist/*.msi` |
| Windows arm64 | `make windows-native` num Windows ARM (Qt arm64 só existe para MSVC) + `make msi-stage` | `dist/*.msi` |
| tudo que sai de Docker | `make dist` | `dist/` |

`packaging/build.py` (só biblioteca padrão) resolve a versão, baixa o modelo,
monta o runtime Python do auxiliar (`pip --platform` com wheels binárias) e
desenha a arte dos instaladores a partir do logo. Os GUIDs do MSI
(`packaging/windows/msi.py`) e o `BUNDLE_ID` `br.com.photoeditor.desktop`
nunca mudam.

## Versão: quem manda é o GitHub

Só existe versão nova quando uma Release é publicada no GitHub:

| Build | Versão exibida no app | Nome do pacote |
|---|---|---|
| Release publicada (CI) | `1.2.3` (a tag) | `PhotoEditor-1.2.3-<alvo>` |
| Local (`make`) | `1.2.3-dev+<commit>` | `PhotoEditor-dev-v1.2.3-<alvo>` |

A última Release vem da API do GitHub e fica em cache (`.cache/latest-release`)
por 24 h (1 h enquanto não houver nenhuma); `make version-refresh` atualiza na
hora. O número vai para o `VERSION` da raiz (ignorado pelo git), que o
`CMakeLists.txt` lê.

## Release no GitHub

`.github/workflows/release.yml`: ao **publicar uma Release**, o
`package.yml` gera os seis instaladores com os mesmos alvos do `make` e os
anexa à Release com o `.sha256` de cada um. O job do macOS exporta antes o
SDK num runner macOS; o Windows arm64 compila num runner `windows-11-arm`.
Para refazer os instaladores de uma Release: Actions → *Run workflow*,
informando a tag.
