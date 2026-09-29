# TODO — PhotoEditor

Controle dos requisitos do editor de fotos de eventos (estilo Lightroom).
Executamos **um item por vez**, na ordem abaixo; cada item concluído é marcado
com `[x]` no mesmo commit que o implementa.

Legenda: `[ ]` pendente · `[~]` em andamento · `[x]` concluído

---

## Contexto: a pasta do projeto

O editor é um **app desktop** (Windows, macOS, Linux): Python e Chromium
embarcados, sem Docker. Cada evento é uma pasta escolhida no próprio app
(Home, menu Arquivo ou linha de comando):

```
<projeto>/
    raw/            fotos originais (somente leitura para o editor)
    project_data/   banco SQLite + caches gerados (thumbnails, previews)
    deleted/        fotos marcadas como excluídas (movidas, nunca apagadas)
    publicar/       saída do botão Exportar
```

Itens antigos abaixo citam `/project` — era o ponto de montagem da pasta no
container; hoje é a pasta do projeto aberto.

---

## Decisões tomadas

- **Banco:** SQLite em `<projeto>/project_data/db.sqlite3` (o catálogo do
  evento) + `~/.photoe/photoe.db` (a lista de projetos do app).
- **Formato das fotos em `raw/`:** somente JPEG.
- **Histórico do CTRL/CMD+Z:** persistido no banco (sobrevive a recarregar).
- **Pasta de exportação:** `publicar/`.

---

## 1. Infra e dados

- [x] **1.1** Montar `/project` no container do backend (volume nos
      `docker-compose*.yml`, caminho local configurável no `.env`, ex.:
      `PROJECT_DIR=~/Storage/teste`).
- [x] **1.2** Banco SQLite em `/project/project_data/db.sqlite3`, criado na
      primeira execução se não existir.
- [x] **1.3** No boot, o backend roda `makemigrations` + `migrate` para manter
      o banco atualizado.
- [x] **1.4** Criar as subpastas `project_data/`, `deleted/` e `publicar/` se
      faltarem; `raw/` ausente gera erro claro no log, sem derrubar a aplicação.

## 2. Catálogo das fotos originais

- [x] **2.1** Modelo `Photo`: nome do arquivo, caminho relativo, tamanho,
      dimensões, orientação EXIF, data/hora de captura
      (`DateTimeOriginal` + `SubsecTimeOriginal`, para ordenar como o evento
      aconteceu) e status (`active` / `deleted`).
- [x] **2.2** Varredura de `/project/raw` (somente JPEG: `.jpg`/`.jpeg`, sem
      diferenciar maiúsculas): cataloga fotos novas, marca as que
      sumiram, não duplica as já conhecidas (idempotente). Roda no boot e por um
      endpoint "reescanear".
- [x] **2.3** Derivados em cache em `project_data/` (thumbnail para a
      filmstrip e preview para o editor), gerados sob demanda, **sem nunca
      alterar o original**.
- [x] **2.4** API: listar fotos (ordenadas pela captura), obter uma foto,
      servir thumbnail / preview / original.

## 3. Layout do editor (referência: `temp/IMG_5156.WEBP`)

- [x] **3.1** Parte superior com **70% da altura** da tela:
    - à esquerda, a foto **original** (sem edição);
    - ao centro/direita, a foto **editada**;
    - na extrema direita, o **painel de controles de edição** (seção Edição).
- [x] **3.2** A tela do editor tem rota própria por foto (ex.: `/photos/:id`),
      de modo que a navegação atualize a URL (regra 3 do CLAUDE.md).
- [x] **3.3** Comportamento no celular/tablet (regra 2.4): definir como
      antes/depois e painel se reorganizam em tela estreita. Abaixo de `lg:`
      as duas fotos ficam lado a lado numa faixa, o painel desce para baixo
      delas e a página rola.
- [x] **3.4** Remover a barra lateral para ganhar espaço de tela: o
      `AppLayout` fica só com cabeçalho + conteúdo.

## 4. Filmstrip de thumbnails (referência: `temp/Xnip2026-09-23_15-33-04.png`)

- [x] **4.1** Parte inferior (os 30% restantes) com a faixa de thumbnails.
- [x] **4.2** A foto em edição fica **sempre ao centro** e destacada; à
      esquerda as anteriores, à direita as seguintes.
- [x] **4.3** Clique em um thumbnail abre aquela foto no editor.
- [x] **4.4** Contador no estilo "N fotos / posição atual".
- [x] **4.5** Ordenar a filmstrip pela barra inferior: data/hora (↑/↓) ou
      nome (A–Z/Z–A, em ordem natural). Navegação, contador e a vizinha
      aberta após excluir seguem a ordem escolhida, lembrada no navegador.

## 5. Atalhos de teclado

- [x] **5.1** `→` — próxima foto.
- [x] **5.2** `←` — foto anterior.
- [x] **5.3** `DEL` (ou `⌫` no Mac) — marca a foto como excluída: **move** o arquivo de
      `raw/` para `deleted/` (nunca apaga) e avança para a próxima.
- [x] **5.4** `CTRL+Z` / `CMD+Z` — desfaz a última ação (exclusão, ajuste,
      preset, auto…). Desfazer uma exclusão devolve o arquivo para `raw/`.
- [x] **5.5** Modelo de histórico de ações (tipo, foto, estado anterior) que
      sustenta o desfazer, persistido no banco.
- [x] **5.6** Atalhos não disparam enquanto o foco está em um campo de texto
      ou slider.
- [x] **5.7** Histórico **completo** das ações de cada foto guardado no banco
      do projeto (`project_data/db.sqlite3`), sobrevivendo ao fechamento e à
      reabertura do projeto: nada é podado, e desfazer marca a ação como
      desfeita (com data) em vez de apagar o registro.
- [x] **5.8** `A` — aplica o **Auto** na foto atual (desfazível como o botão).

## 6. Edição

As definições **não são gravadas na foto**: ficam no banco, numa relação da
foto com cada ajuste. A foto editada exibida é renderizada a partir do
original + ajustes.

- [x] **6.1** Modelo de ajustes por foto (`Photo` → ajustes), com valores
      neutros por padrão e o preset aplicado, se houver.
- [x] **6.2** Renderização do preview editado no backend a partir do
      original + ajustes (mesmo motor usado na exportação, para o que se vê
      ser o que se exporta).
- [x] **6.3** **Auto** — portar o corretor `photofix_v2` do
      `../correct-photos` (`src/photofix/enhance.py` / `pipeline.py`):
      curva de tom que preserva o ponto branco e exposição medida no sujeito
      (`subject.py`). O resultado vira valores de ajuste editáveis, não um
      "carimbo" irreversível.
- [x] **6.4** Ajustes manuais:
    - [x] Saturação
    - [x] Levels (Shadows, Highlights, Blacks, Whites, Exposure, Contrast…)
    - [x] Outros controles: Temperatura, Matiz (tint) e Vibratilidade
- [x] **6.5** **Presets** — conjunto inspirado nos perfis mais comuns dos
      editores populares (Lightroom/VSCO: vívido, suave, P&B, quente, frio,
      vintage/matte…). Lista final: Vívido, Suave, Quente, Frio, P&B, Matte e
      Vintage — deslocamentos somados aos ajustes da foto
      (`imaging/develop.py:PRESETS`), então convivem com o Auto.
- [x] **6.6** Botão para resetar a foto (volta ao neutro, desfazível).
- [x] **6.7** **Crop** mantendo a proporção da foto: o quadro fica sobre a
      original (esquerda) e a editada (direita) mostra o recorte ao vivo.
      Arrastar move, os cantos redimensionam com o canto oposto fixo e
      arrastar fora do quadro gira **só o quadro** (a foto fica parada), até
      ±90°. A imagem nunca gira: múltiplos de 90° só trocam o recorte entre
      retrato e paisagem, e o resto (até 45°) endireita, como o Straighten do
      Lightroom. Atalho `C` entra/sai do modo (e `Esc` sai); no modo, `←`/`→` giram
      o quadro de 15 em 15°. Gravado no banco, desfazível, e aplicado igual no
      preview e na exportação (que recorta na resolução cheia antes de
      reduzir para 1920×1080).

## 7. Exportação

- [x] **7.1** Botão **Exportar** que gera todas as fotos ativas (não
      excluídas) com seus ajustes em `/project/publicar/`.
- [x] **7.2** Mesmo padrão de saída do `../correct-photos`
      (`src/photofix/publish.py`): JPEG numa caixa 1920×1080, 72 dpi,
      qualidade 88, `optimize` e `progressive`; **EXIF preservado**
      (`DateTimeOriginal`/`SubsecTimeOriginal`/`OffsetTimeOriginal`); pixels
      girados de vez com `Orientation = 1`; miniatura embutida removida.
- [x] **7.3** Progresso visível durante a exportação (pode levar minutos) e
      resumo ao final, usando os modais do sistema (regra 1).
- [x] **7.4** Reexportar sobrescreve apenas as fotos alteradas desde a última
      exportação: cada foto guarda o hash do que foi escrito (arquivo +
      ajustes + versões do motor e do formato) e é pulada se nada mudou; a
      cópia exportada de uma foto excluída depois sai de `publicar/`.

## 8. Camadas

- [x] **8.1** Selecionar uma ou mais áreas da foto com um pincel e extraí-las
      numa **camada**; o restante da foto fica em outra, e cada uma tem os
      próprios ajustes (sliders, presets e Auto). Algoritmo avaliado em
      2026-09-24 nas fotos do evento: GrabCut (OpenCV) vaza para fundos com
      textura e os modelos do Ollama não geram máscara por pixel; ficou o
      SAM 2.1 tiny em ONNX, local em CPU, com o traço virando vários prompts
      e vencendo a máscara que melhor coincide com a área pintada. Máscaras
      em `project_data/masks/`, gravadas no estado da foto (histórico,
      desfazer, render e exportação). `S` entra/conclui o modo seleção, `L`
      passa para a próxima camada.

## 9. App desktop (sem Docker)

- [x] **9.1** Python embarcado (CPython 3.12 do python-build-standalone) e
      Chromium embarcado (Qt WebEngine/PySide6), endurecido: sem barra de
      endereço, abas, menu de contexto ou DevTools; navegação presa à origem
      do servidor local; link externo vai para o navegador do SO; permissões
      de página negadas.
- [x] **9.2** Backend servido pelo waitress em `127.0.0.1`, num processo por
      projeto, protegido por token de sessão (cookie `HttpOnly`) e Host fixo.
      Docker, nginx, uWSGI e os compose saem do projeto.
- [x] **9.3** Tela **Home** no estilo da referência (CapCut): Abrir projeto,
      Novo projeto (copia os JPEGs para `raw/`) e cards dos projetos recentes
      com capa, nº de fotos, tamanho e data. Lista em `~/.photoe/photoe.db`.
- [x] **9.4** Builder em Docker (`make dist`) que monta o pacote
      das quatro plataformas num só container, sem compilar nada.
- [x] **9.5** Instaladores gerados no builder: macOS `PhotoEditor.app` num
      `.dmg` com a janela "arraste para Aplicativos" (fundo, ícones e seta
      desenhados a partir do logo); Windows `.msi` (Program Files, atalhos no
      Menu Iniciar e na Área de Trabalho, upgrade/downgrade controlados).
- [ ] **9.6** Assinatura: Developer ID + notarização no macOS; code-signing
      do `.msi` no Windows (hoje o Gatekeeper e o SmartScreen avisam).
- [ ] **9.7** Instalador Linux (AppImage e/ou `.deb`, com as libs do SO que o
      Qt WebEngine exige) e teste do pacote num Ubuntu limpo.
- [ ] **9.8** Enxugar o PySide6 (tirar módulos Qt que o app não usa) — o
      runtime tem ~1,5 GB.

## 10. Edição — ajustes de uso

- [x] **10.1** Painéis redimensionáveis no desktop (fotos × filmstrip,
      Original × Editada, largura do painel), com tamanhos lembrados,
      duplo clique para o padrão e setas do teclado.
- [x] **10.2** Foto de capa: uma por projeto, desfazível, selo na
      filmstrip; o Exportar a grava com o nome original **e** como
      `publicar/capa.jpg`.
- [x] **9.9** `make dist` com cinco alvos (macOS arm64/x64, Windows x64,
      Linux x64/arm64), `make local` (só a plataforma da máquina) e o
      workflow que anexa os instaladores a cada Release publicada. Windows
      ARM64 aguarda wheel `win_arm64` do OpenCV.
- [x] **9.10** O GitHub manda na versão: sem `VERSION` versionado nem bump
      por commit; a versão sai das Releases (tag na Release; última Release
      + `-dev` nos builds locais). Instaladores só na Release publicada.
