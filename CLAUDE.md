# Instruções do Projeto

Editor de fotos de eventos, no estilo Lightroom, distribuído como **app
desktop nativo** (Windows, macOS e Linux, cada um em x64 e arm64): **C++20 +
Qt 6 (Qt Quick/QML)**, OpenCV estático e ONNX Runtime — sem navegador
embarcado e sem servidor local. O modelo de projeto (CMake, Docker para
compilar e empacotar, QML com tema em singleton) é o do `../clivcut`. As
convenções abaixo são obrigatórias e devem ser replicadas em qualquer projeto
derivado deste.

A versão anterior (Python/Django + React + Qt WebEngine) está preservada na
branch `legacy` — referência para comportamento e matemática
(`git show legacy:<caminho>`). Não há login, conta nem permissionamento: nada
de introduzir essas camadas sem pedido explícito.

## Estrutura

Na raiz ficam **só** estas pastas (mais `CMakeLists.txt`, `Makefile`,
`README.md`, `LICENSE`, `CLAUDE.md` e os gerados/ignorados `build/`, `dist/`,
`.cache/`):

```
.claude/  .github/  docs/  i18n/  packaging/  src/  tests/
```

```
src/imaging/    processamento de imagem (OpenCV + QtCore): revelação, crop,
                Auto, Merge, SAM (ONNX), máscaras, JPEG/EXIF, exportação
src/core/       projeto + catálogo SQLite, histórico/desfazer, merges,
                caches derivados, Exporter, projetos recentes
src/app/        main, controllers C++ expostos ao QML, image providers,
                qml/ (Theme, componentes, páginas), icons/ (Lucide), assets/, fonts/
src/instagram/  auxiliar Python do Instagram (única parte em Python)
src/cmake/      módulos CMake (ONNX Runtime, Packaging)
packaging/      builders Docker por plataforma, build.py, deps, arte dos instaladores
i18n/           traduções Qt Linguist (.ts)
tests/          QtTest; tests/data/ com referências geradas pela versão Python
docs/           ARCHITECTURE.md, TODO.md, images/
```

Coisa nova do projeto vai para dentro de `src/` (ou `packaging/`, `tests/`,
`docs/`, `i18n/`) — nunca uma pasta nova na raiz.

## UI / UX

### 1. Modais próprios — nunca diálogos do sistema
Toda confirmação, alerta, aviso ou mensagem ao usuário usa os modais do
próprio app, consistentes com o design.

- **Proibido:** `MessageDialog` do QtQuick.Dialogs, `QMessageBox` ou qualquer
  popup nativo do SO para confirmar/avisar. (Seletor de pasta/arquivo nativo —
  `FolderDialog`/`FileDialog` — é permitido: é escolha de arquivo, não aviso.)
- **Como aplicar:** `src/app/qml/Dialogs.qml` (singleton; o `DialogHost` é
  montado uma vez no `Main.qml`) e `components/Modal.qml`:

  ```qml
  Dialogs.confirm({
      title: qsTr("Delete photo?"),
      description: qsTr("Delete <b>%1</b>?").arg(Messages.escapeHtml(name)),
      variant: "danger",            // danger | warning | success | info
      confirmLabel: qsTr("Delete"),
      onConfirm: (done) => { App.editor.deleteCurrent(); done(true) }
  })
  Dialogs.alert({ title: qsTr("Could not save"), variant: "danger" })
  ```
- Erros das ações chegam do C++ pelo sinal `failed(key, params)` (chave de
  i18n); o `Main.qml` mostra o modal com `Messages.errorDialog(key, params)`.

### 2. Telas ocupam 100% da área disponível
- **Proibido:** "cards" estreitos e centralizados com grandes margens vazias;
  largura máxima fixa em formulários.
- **Como aplicar:** páginas em `ScrollPage`/layouts com `Layout.fillWidth`;
  para agrupar campos, `GridLayout` responsivo em vez de limitar a largura.

### 2.1. Cor: vermelho é acento, esmeralda é confirmação, erro tem forma própria
Cores só no `src/app/qml/Theme.qml` (claro e escuro); componentes nunca
escrevem hex. O vermelho da marca (`Theme.brand50..brand900`, base `#ef3236`)
é **acento**: item ativo, seleção, destaque.

- **Confirmação/sucesso em `Theme.emerald*`.**
- **Erro nunca é só texto colorido:** `components/FormError.qml` — caixa com
  fundo, borda e **ícone obrigatório**.

> **Por quê:** o vermelho da marca fica a ΔE 7,6 do `red-500`; sem uma forma
> que os separe, "confirmado" e "falhou" ficariam idênticos. O ícone carrega o
> significado para quem não distingue vermelho de verde.

### 2.2. Booleano é sempre o Toggle
`components/Toggle.qml` (e `ToggleField.qml`) é o controle de todo campo
booleano. **Proibido** `CheckBox`/`Switch` crus do Controls. O `Toggle` é
controlado: `onToggled: (value) => campo = value`.

### 2.3. Seleção em lista é o círculo com ✓, não o Toggle
`components/SelectionCheck.qml` marca linhas que entram no conjunto a operar
(filmstrip no modo merge, versões a publicar). Quem recebe o clique é a LINHA;
a seleção só aparece quando alguém entra no modo.

### 3. Detalhe de objeto é uma página própria
Detalhe/edição de uma entidade é uma página (`App.page`), nunca um modal.
Modais ficam para confirmações e mensagens curtas.

## Internacionalização (i18n)

### 4. Todo texto visível é traduzível; inglês é o padrão e o fallback
Idiomas: **EN** e **PT-BR** (lista extensível em `src/app/Language.cpp`).

- **QML:** `qsTr("English text")`; plurais com `qsTr("%n photos", "", n)`.
  **C++:** `tr()` em QObject, `QCoreApplication::translate()` fora deles.
  O texto-fonte é **sempre em inglês**; nada de PT hard-coded.
- Traduções em `i18n/photoeditor_pt_BR.ts` (Qt Linguist); `photoeditor_en.ts`
  só guarda plurais. Atualize com `make translations` e traduza as entradas
  novas no mesmo commit.
- Idioma: escolha salva (QSettings) → idioma do SO → inglês. Trocar no app
  retraduz na hora (`QQmlEngine::retranslate`).
- O auxiliar do Instagram devolve só **chaves** de erro; o texto é do QML
  (`Messages.qml`).

## Identidade visual

### 5. Logo, ícones e fonte no binário — o app funciona offline
Nada da interface depende de rede: logo (`src/app/assets/logo/photoe-{light,dark}.png`,
o "dark" tem texto branco), ícones Lucide em `src/app/icons/*.svg` (servidos
recoloridos por `image://icon/<nome>?color=...`) e a fonte **Inter**
(`src/app/fonts/`) vão como recursos do módulo QML. Ícone novo: o SVG oficial
do Lucide, mesmo nome, com o `LICENSE` junto.
Os ícones dos instaladores (`.icns`, `.ico`, fundo do DMG) são **desenhados em
código** a partir do logo (`packaging/art.py`) — não versione PNG/ICNS/ICO prontos.

## Dados e compatibilidade

### 6. A pasta do projeto e a pasta da máquina
Cada evento é uma pasta que a pessoa escolhe:

```
<projeto>/raw/            originais (somente JPEG, nunca alterados)
<projeto>/project_data/   db.sqlite3 + cache/ + masks/ + trava (.photoeditor.lock)
<projeto>/deleted/        fotos excluídas (movidas, nunca apagadas)
<projeto>/publicar/       saída do Exportar (publicar/instagram/ = versões do feed)
```

O que é da máquina fica em `~/.photoe/` (igual nos três SOs; sobrescrevível
por `PHOTOEDITOR_DATA_DIR`): `photoe.db` (projetos da Home), `covers/`,
`logs/`, `instagram.json` (sessão, modo 0600). Preferências de interface
(idioma, tema, layout dos painéis) no `QSettings`.

- **Sem `.env`:** o app não lê arquivo de configuração. Ajuste de
  desenvolvimento é por variável de ambiente (`PHOTOEDITOR_DATA_DIR`,
  `PHOTOEDITOR_MODELS_DIR`, `PHOTOEDITOR_LOG_LEVEL`, `PHOTOEDITOR_PYTHON`) ou
  argumento (`--debug`).
- Caminhos sempre por `pe::ProjectPaths` / `pe::AppPaths` (`src/core/Paths.h`),
  nunca montados à mão. Toda escrita vai para `~/.photoe/` ou para a pasta do
  projeto — nunca para a instalação.

### 7. Compatibilidade com os projetos da versão anterior
Projetos abertos pela versão Python abrem aqui **sem conversão**: mesmo
esquema (tabelas `photoeditor_*`, ids UUID hex de 32 caracteres, datas UTC no
formato do Django), mesmos nomes de cache (`<uuid com hífens>-<mtime_ns>.jpg`)
e mesmos hashes (`settings_hash`, `exported_hash`, `align_version`, montados
como o `json.dumps` do Python — ver `imaging/SettingsHash.cpp`). Quem atualiza
não reexporta tudo à toa.

- O `mtime` é sempre em **nanossegundos** (`pe::fileMtimeNs`, igual ao
  `st_mtime_ns`); o `QFileInfo` só tem milissegundos.
- Dentro dos JSON (merge, histórico) os ids vão **com hífens** (o `str(uuid)`
  do Python); no banco, sem. `Project::normalizeId`/`dashedId` convertem.
- O teste `tst_core::legacyCatalogOpensWithoutChanges` prova isso com um banco
  gerado pela versão Python (`tests/data/core/`). Não o quebre.

### 8. Migrations incrementais
O `db.sqlite3` de cada projeto é dado real e persiste entre versões. O esquema
mora em `src/core/schema/NNNN.sql` e migra pelo `PRAGMA user_version`
(passos 1–12 = as migrations Django da versão anterior; banco legado começa do
que a `django_migrations` registra). O `photoe.db` da máquina segue o mesmo
princípio (`src/core/RecentProjects.cpp`).

- **Proibido:** editar ou apagar um passo já publicado.
- **Como aplicar:** mudou o esquema → arquivo `NNNN.sql` novo, `kSchemaSteps`
  em `src/core/Database.cpp` + 1, teste em `tst_core`, tudo no mesmo commit.

### 9. Banco só na thread principal; o pesado roda fora dela
`QSqlDatabase` não cruza threads. Controllers montam na thread principal tudo
de que um trabalho precisa (`RenderRequest`, `ExportJob`, `MergeSnapshot`) e o
trabalho roda em pool (`QtConcurrent`/`QThreadPool`), devolvendo o resultado
por sinal/`invokeMethod` em fila. A interface nunca trava: imagens geradas vêm
de providers assíncronos (`image://photo/...`, `src/app/ImageProviders.h`).

### 10. Log vai para `~/.photoe/logs/`
O app instalado não tem terminal: `app.log` rotativo (5 MB × 3) e, quando há
terminal, também o stderr (`src/app/Logging.cpp`); o auxiliar do Instagram
escreve `instagram.log`. Nos módulos: `Q_LOGGING_CATEGORY(lcX, "photoeditor.x")`
+ `qCInfo`/`qCWarning`. **Proibido** `printf`/`std::cout` para diagnóstico.
Nunca logar senha, cookie ou sessão.

## Convenções de código e versionamento

### 11. Estilo do código (o do clivcut)
- Todo arquivo começa com `// SPDX-License-Identifier: BSD-2-Clause`;
  headers com `#pragma once`; includes do projeto (`"core/Project.h"`), depois
  Qt, depois std.
- Namespaces `pe`, `pe::imaging`, `pe::app`; classes PascalCase, funções e
  variáveis camelCase, membros `m_`, constantes `k`. Um par `Foo.h/.cpp` por
  unidade. Imagens são `cv::Mat` **RGB** (nunca BGR na API) — converta na
  borda do `imencode`/`imdecode`.
- **Identificadores em inglês; comentários em português.**
- Caminhos com `QString`/`QFile` (+ `cv::imdecode`) — nunca `cv::imread`
  com `std::string`, que quebra com acentos no Windows.

### 12. Mudou a matemática? Compare com a versão Python
Revelação, crop, Auto, Merge e SAM são portes 1:1 da branch `legacy`. Os
testes (`tests/tst_develop`, `tst_auto`, `tst_merge`, `tst_segment`) comparam
com referências geradas por ela (scripts `gen_*.py` em `tests/data/`).
Mudança que altere o resultado de um mesmo ajuste sobe `kEngineVersion`
(`imaging/Develop.h`) — invalida os caches e força a reexportação.

### 13. Ferramenta não instalada na máquina? Docker
Docker é ferramenta de **desenvolvimento e build**, nunca de execução do app.
Sem Qt/OpenCV no host, `make build`, `make test` e `make run` rodam no builder
Linux (`packaging/linux/Dockerfile`); `make shell` abre um shell nele. Para
qualquer outro runtime (Python das referências, node, actionlint), imagem
oficial descartável com `--rm` e volume no projeto.

### 14. Commits vão direto na `main`
Fluxo trunk-based no `git@gitlab.com:saas-sec4us/photo-editor.git`.
- **Proibido:** branch de feature ou merge request para publicar uma alteração.
- **Como aplicar:** quando o usuário pedir, `git add` + `git commit` +
  `git push origin main`, sem `--author`, mensagens em português.

### 15. O GitHub manda na versão
A versão **não mora no repositório**: publicar uma Release `vX.Y.Z` no GitHub
cria uma versão; entre Releases, todo build é "a última Release + o canal".

| Canal | Onde | O app exibe | Pacote |
|---|---|---|---|
| `release` | CI, ao publicar a Release | `1.2.3` | `PhotoEditor-1.2.3-<alvo>` |
| `dev` | máquina local (`make`) | `1.2.3-dev+<commit>` | `PhotoEditor-dev-v1.2.3-<alvo>` |

- **Proibido:** versionar o `VERSION`, subir número "na mão" ou gravar versão
  fixa no código.
- **Quem resolve:** `packaging/build.py version` (cache de 24 h; `make
  version-refresh` força; `PHOTOEDITOR_VERSION`/`PHOTOEDITOR_CHANNEL`
  sobrescrevem). Ele grava o `VERSION` da raiz, que o `CMakeLists.txt` lê.
- **Formato:** `X.Y.Z` com X ≤ 255 e Y, Z ≤ 999 (limite do MSI).

## App desktop e empacotamento

### 16. Builders em Docker, no modelo do clivcut
Compilar e gerar os instaladores sai de builders Docker em `packaging/<plataforma>/`,
chamados pelo `Makefile` (a base de compilação: `make help` lista tudo; passo
novo de build, teste ou empacotamento entra como alvo nele).

| Alvo | Como | Instalador |
|---|---|---|
| linux-x64 / linux-arm64 | `make linux` (`ARCHS=...`), builder Linux | `.tar.gz` |
| macos-x64 / macos-arm64 | `make dmg` / `make dmg-all`, cross-compile com o SDK deste Mac (`make macos-sdk`) | `.dmg` |
| windows-x64 | `make windows`, cross-compile llvm-mingw | `.msi` (wixl) |
| windows-arm64 | `make windows-native` num Windows ARM (só há Qt arm64 para MSVC) + `make msi-stage` | `.msi` |

- **CI no GitHub:** só o `release.yml` (ao publicar a Release) chama o
  `package.yml`, que roda os alvos do `make` e anexa instaladores + `.sha256`.
  Workflows só chamam o `make`.
- **Versões fixas:** Qt, OpenCV (`packaging/deps/opencv.sh`), ONNX Runtime
  (`src/cmake/OnnxRuntime.cmake`), imagens base datadas, CPython do auxiliar e
  modelo SAM (revisão + sha256) em `packaging/build.py`. Trocar é decisão
  explícita, nunca efeito colateral de um build.
- **Nunca troque os GUIDs de `packaging/windows/msi.py`** (`UpgradeCode` e
  componentes dos atalhos) nem o `BUNDLE_ID` `br.com.photoeditor.desktop`: é
  por eles que o Windows e o macOS reconhecem a versão nova como o mesmo programa.
- Dependência nova (C++ ou do auxiliar Python) precisa existir para os **seis**
  alvos; confira antes de adotar.

### 17. Instagram: a única parte em Python
A conversa com o Instagram (API privada, `instagrapi`) é um processo auxiliar
(`src/instagram/photoe_instagram.py`, CPython embarcado no pacote) que fala
**JSON por linha no stdin/stdout** com o `InstagramController` — sem servidor
HTTP. Exportar, validar o feed e gravar o post são do C++. O stdout do
auxiliar é só protocolo; log vai para o stderr/arquivo. Teste offline:
`PHOTOE_INSTAGRAM_FAKE=1` (`src/instagram/test_protocol.py`).

### 18. Como validar
- `make test` — todos os testes (QtTest; referências da versão Python).
- `make run ARGS="--screenshot /tmp/x.png [pasta-do-projeto]"` abre o app,
  fotografa a primeira tela e sai — teste de fumaça sem ninguém olhando. Sem
  Qt no host, roda no builder (xvfb).
- Mudou tela: compare com `docs/images/screen*.png`.
