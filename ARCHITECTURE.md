# PhotoEditor — arquitetura

Documento para quem desenvolve o PhotoEditor. O uso do app está no
[README](./README.md); as convenções obrigatórias do código, no
[CLAUDE.md](./CLAUDE.md).

Editor no estilo Lightroom (Django REST + React) empacotado como **app desktop
nativo** para Windows, macOS e Linux: Python embarcado + Chromium embarcado
(Qt WebEngine), sem Docker, sem navegador e sem nada instalado na máquina de
quem usa. A janela não tem barra de endereço nem cara de navegador — é o app.

## Stack

| Camada    | Tecnologia                                                        |
|-----------|-------------------------------------------------------------------|
| Janela    | PySide6 6.11 — Qt WebEngine (Chromium) endurecido, sem barra      |
| Backend   | Django 5.2 + Django REST Framework, servido pelo waitress local   |
| Frontend  | React 19 (CRA/craco), TailwindCSS, react-router                   |
| Banco     | SQLite do evento (`<projeto>/project_data`) + `~/.photoe/photoe.db` |
| Runtime   | CPython 3.12 do python-build-standalone, embarcado no pacote      |
| Imagem    | NumPy, OpenCV, Pillow; SAM 2.1 tiny (ONNX Runtime, CPU)           |

Estrutura:

```
backend/    core/ (settings, wsgi) + photoeditor/ (app: models, views, services)
desktop/    shell nativo: janela, menus, navegador endurecido, servidor local
frontend/   src/ (pages, components/ui, contexts, i18n, lib)
tools/      build.py (runtime, modelo, frontend, pacote), packaging/ (dmg, msi,
            arte) + Dockerfile do builder
.github/    workflows de Release (instaladores anexados à Release)
```

## Processos

```
┌─ shell (desktop/app.py) ─────────────┐        ┌─ servidor (desktop/server.py) ─┐
│ QMainWindow + menus nativos          │ spawn  │ Django + waitress              │
│ QWebEngineView endurecido  ──────────┼──────► │ http://127.0.0.1:<porta>       │
│  • só navega na origem do servidor   │ cookie │ AppTokenMiddleware (token)     │
│  • /__desktop__/<ação> → SO          │ token  │ SQLite do projeto aberto       │
└──────────────────────────────────────┘        └────────────────────────────────┘
```

- **Dois processos, o mesmo Python embarcado.** Trocar de projeto derruba o
  servidor e sobe outro apontando para a nova pasta; sem projeto, ele sobe em
  **modo Home** (só a tela inicial e os projetos recentes). Um crash nativo
  (onnxruntime, OpenCV) derruba o servidor, não a janela.
- **Navegador endurecido** (`desktop/browser.py`): sem barra de endereço,
  abas, menu de contexto ou DevTools; link externo abre no navegador do SO;
  permissões de página (câmera, localização, notificações…) negadas;
  `file:`/`chrome:`/`javascript:` barrados.
- **Servidor só para o app:** escuta em `127.0.0.1`, recusa Host diferente
  (DNS rebinding) e exige o token sorteado a cada execução, entregue ao
  navegador embarcado como cookie `HttpOnly`/`SameSite=Strict`. Um site
  aberto no navegador comum da pessoa não consegue chamar a API.
- **Ponte React → SO:** o React navega para `/__desktop__/<ação>` (abrir
  pasta, novo projeto, voltar ao Início, mostrar no Finder/Explorer) e o
  shell intercepta antes de a navegação sair (`frontend/src/lib/desktop.js`).
  A ação roda **fora** do `acceptNavigationRequest` (`QTimer.singleShot`):
  trocar a página lá dentro derruba o Chromium.

## Projetos e dados

Cada evento é uma pasta de projeto:

```
<projeto>/
    raw/            fotos originais (somente JPEG, nunca alteradas)
    project_data/   db.sqlite3 + caches gerados + masks/ (camadas) + trava
    deleted/        fotos excluídas (movidas, nunca apagadas)
    publicar/       saída do botão Exportar
```

- O app cria `project_data/`, `deleted/` e `publicar/`; `raw/` só é criada
  depois de perguntar. **Novo projeto** numa pasta com JPEGs soltos cria
  `raw/` e **move** as fotos para ela (`desktop/projects.py`: só os da raiz,
  sem sobrescrever; ocultos e subpastas ficam). Pasta que já é projeto
  (`project_data/db.sqlite3` ou `raw/`) só abre.
- Ao abrir o projeto, o servidor roda `migrate` no SQLite dele (nunca
  `makemigrations`) e então o boot (catálogo de `raw/`). Um projeto só abre
  em uma janela por vez (`QLockFile` em `project_data/`).
- O que é da máquina, não do evento, fica em `~/.photoe/` (igual nos três
  SOs): `photoe.db` (lista de projetos da Home, `sqlite3` puro, esquema por
  `PRAGMA user_version`), segredos gerados (`.env`), perfil do navegador
  (`webengine/`), capas dos cards (`covers/`) e `logs/` (`desktop.log` e
  `server.log`).
- A capa do card da Home é lida do catálogo do projeto **somente leitura**
  (`recent_projects.cover_source`); sem capa marcada, a primeira foto.

## Processamento de imagem

### Camadas: SAM 2.1

Quem acha o objeto sob o traço do pincel é o **SAM 2.1 tiny** (Segment
Anything 2, Meta, Apache-2.0) em ONNX, rodando **localmente em CPU** — sem
GPU, sem Ollama e sem internet. O modelo (~155 MB) vai no pacote do app (o
builder o baixa com revisão e hashes fixos — `make model`). Custo: ~1,5 s na
primeira seleção de cada foto e ~0,3 s por traço; pico de ~1,2 GB de RAM
durante a análise. Com **Detectar o objeto (IA)** desligado, o pincel
seleciona exatamente a área pintada.

As máscaras ficam em `project_data/masks/` (PNG, nome = hash do conteúdo) e
entram no histórico, no desfazer e na exportação como qualquer ajuste.

### Merge: alinhamento

Antes de extrair, cada foto é **alinhada à base pelo fundo**: pontos SIFT
filtrados por RANSAC numa transformação de similaridade (deslocamento,
rotação e escala — a câmera na mão anda entre um clique e outro), com a área
selecionada fora da análise. O ganho de exposição/cor também é medido no
fundo. Nas fotos de teste: ~2.100 pontos de fundo por par e erro mediano de
0,2 px no preview.

## Convenções principais

### Acesso público
- Sem login, contas ou permissões: a API não tem classe de autenticação e
  libera tudo (`REST_FRAMEWORK` em `core/settings.py`). A única trava é a de
  transporte (token do app, acima), que não identifica pessoa nenhuma.
- Django admin público: `photoeditor/middleware.py` (`PublicAdminMiddleware`)
  loga toda visita a `/admin/` como o usuário `admin` (senha inutilizável).
- Estáticos do admin (`/django-static/`) e o build do React saem pelo
  WhiteNoise, direto das apps — sem `collectstatic`.

### Internacionalização (EN + PT-BR)
- **EN é o padrão e o fallback** de toda tradução.
- Frontend: `useI18n()` (`t`/`tf`) + catálogos em `src/i18n/locales.js`.
- Backend: `photoeditor/i18n.py` (`translate`, `tr`, `language_for_request`).
- Shell desktop (menus, diálogos nativos): `desktop/i18n.py`, que acompanha o
  idioma escolhido no app observando o cookie `photoeditor_ln`.

### E-mail e identidade visual
- Template HTML de marca em `photoeditor/templates/email/`, renderizado por
  `services/mailer.py`.
- Logo, favicon e a fonte Inter vão no próprio build — o app funciona offline.
  Cache-busting `?ts=<BUILD_TS>` em todo objeto estático.

### UI/UX
- Confirmações/alertas via **modais próprios** (nunca diálogos nativos do browser).
- Telas e formulários ocupam **100%** da largura.
- Detalhe de objeto abre em **janela/rota própria**, não em modal
  (`window.open` vira uma janela do app, igualmente sem barra).

## Compilação: `make`

O **`Makefile` é a base de compilação**: `make` sozinho gera os instaladores
(o mesmo que `make dist`) e `make help` lista os alvos.
Pré-requisitos: `make`, `python3` (qualquer 3.9+) e Docker (para o frontend,
os testes do React e os instaladores). Node não precisa estar instalado.

| Comando | O que faz |
|---|---|
| `make run` | abre o app (baixa runtime e modelo e builda o frontend, se faltar) |
| `make dev` | idem, com DevTools (F12) e menu de contexto |
| `make run ARGS="~/Fotos/evento"` | já abrindo um projeto |
| `make frontend` | build do React em `frontend/build` |
| `make dist` (ou só `make`) | instaladores de todas as plataformas em `dist/` |
| `make local` | só o instalador da plataforma e arquitetura desta máquina |
| `make macos` / `make windows` / `make linux` | `.dmg` (arm64 + x64) / `.msi` / `.tar.gz` (x64 + arm64) |
| `make dist TARGETS="macos-arm64 windows-x64"` | só os alvos escolhidos |
| `make test` | `check` do Django, migrations em dia, build e testes do React |
| `make migrations` | gera a migration depois de mudar um modelo |
| `make art` | prévia da arte dos instaladores em `.cache/art` |
| `make version` | mostra a versão do build (a última Release do GitHub + `-dev`) |
| `make clean` / `make clean-all` | apaga o gerado / também runtimes, downloads e modelo |

O runtime do host fica em `.runtime/<alvo>/` (CPython 3.12 + dependências). Com
hot-reload do React: `yarn start` no `frontend/` e
`make run ARGS="--port 47823 --frontend-url http://127.0.0.1:3000"`.

## Pacotes (builder em Docker)

Um container Linux monta o pacote de **todas** as plataformas — nada é
compilado: o Python de cada alvo é o CPython pronto do python-build-standalone
e as dependências são wheels binárias baixadas com `pip --platform <alvo>`.

```bash
make dist                                   # todos os alvos
make windows                                # só o .msi
make dist TARGETS="macos-arm64 linux-x64"   # alvos escolhidos
```

Saída em `dist/`, um instalador por SO:

| Alvo          | Arquivo                                   | O que é                              |
|---------------|-------------------------------------------|--------------------------------------|
| `macos-arm64` | `PhotoEditor-<versão>-macos-arm64.dmg`    | `PhotoEditor.app` + janela "arraste para Aplicativos" |
| `macos-x64`   | `PhotoEditor-<versão>-macos-x64.dmg`      | o mesmo, para Mac Intel |
| `windows-x64` | `PhotoEditor-<versão>-windows-x64.msi`    | instala em `Program Files`, atalhos no Menu Iniciar e na Área de Trabalho |
| `linux-*`     | `PhotoEditor-<versão>-linux-*.tar.gz`     | pasta com `runtime/`, `app/` e o lançador `PhotoEditor` |

`--keep-dirs` mantém também a pasta aberta do pacote, para testar no lugar.

- **`.dmg`** — montado no Linux como o do Firefox: `mkfs.hfsplus` cria o volume
  HFS+, o `hfsplus` do libdmg-hfsplus o preenche e o `dmg` comprime. A janela
  que o Finder abre ao montar (fundo, tamanho, posição dos ícones) é o
  `.DS_Store` escrito em `tools/packaging/macos.py`; o fundo e os ícones são
  desenhados a partir do logo em `tools/packaging/art.py`
  (`make art` gera uma prévia). O `.app` é gerado por arquitetura
  (`LSArchitecturePriority` + `arch -<arch>` no lançador).
- **`.msi`** — gerado pelo `wixl` (msitools). Instala por máquina; o
  `UpgradeCode` fixo faz a versão nova substituir a anterior e recusa
  downgrade. A `ProductVersion` é derivada da versão (o MSI só aceita
  `255.255.65535`; ver `msi_version`).
- **Sem assinatura, por enquanto:** assinar e notarizar exige certificados
  (Apple Developer ID e um code-signing do Windows) — próxima etapa.

**Espaço em disco:** cada alvo ocupa ~1,5 GB de runtime em
`.runtime-builder/` (reaproveitado entre builds) e ~0,5–0,7 GB de pacote. Os
cinco alvos pedem ~15 GB livres — no macOS, também dentro do disco do Docker
Desktop. Rode um build por vez: dois em paralelo estouram a memória do Docker.

| Alvo          | SO mínimo (ditado pelas wheels)                          |
|---------------|----------------------------------------------------------|
| `windows-x64` | Windows 10/11 64 bits                                    |
| `macos-arm64` | macOS 14 (Apple Silicon)                                 |
| `macos-x64`   | macOS 14 (Intel) — usa o onnxruntime 1.23.x, o último com wheel Intel |
| `linux-x64`   | glibc 2.34 — Ubuntu 22.04, Debian 12, Fedora 35          |
| `linux-arm64` | glibc 2.39 — Ubuntu 24.04                                |

**Windows ARM64 não é gerado:** o OpenCV (motor de revelação, merge, camadas)
não publica wheel para `win_arm64`. O `.msi` x64 roda no Windows 11 ARM pela
emulação x64 do próprio Windows.

## Versão: quem manda é o GitHub

A versão não fica no repositório: **só existe versão nova quando uma Release é
publicada no GitHub**. Entre uma Release e outra, todo build usa o número da
última Release e diz de onde veio (`tools/build.py`, regra 17 do CLAUDE.md):

| Build | Versão exibida no app | Nome do pacote |
|---|---|---|
| Release publicada (CI) | `1.2.3` (a tag) | `PhotoEditor-1.2.3-<alvo>` |
| Local (`make`) | `1.2.3-dev+<commit>` | `PhotoEditor-dev-v1.2.3-<alvo>` |

Sem nenhuma Release publicada ainda, o número é `0.0.0`.

## Release no GitHub

`.github/workflows/release.yml`: ao **publicar uma Release**, o GitHub Actions
gera os cinco instaladores (um job por alvo, em paralelo, no mesmo builder
Docker do `make dist`, via `package.yml`) e os anexa à Release, cada um com o
seu `.sha256`.

- **A versão é a da tag** criada no GitHub (`v1.2.3` ou `1.2.3`): o pipeline
  a grava no `VERSION` antes do build, e ela vira o nome dos instaladores, o
  número no cabeçalho do app e a versão do `.app`/`.msi`. Formato `X.Y.Z`,
  com X até 255 e Y, Z até 999 (limite do MSI); fora disso o workflow para
  antes de gerar qualquer coisa. Nada é commitado de volta.
- Para refazer os instaladores de uma Release: Actions → "Release installers"
  → *Run workflow*, informando a tag.
- O repositório é público: os minutos do Actions são gratuitos. Na Release,
  cada arquivo pode ter até 2 GiB, sem limite de total nem de download.

## Banco e migrations

- Modelos em `photoeditor/dbmodels/` (herdam de `dbmodels.base.Base`); o único
  usuário é o `admin` no `auth.User` do Django.
- Migrations incrementais e versionadas (regra 13 do `CLAUDE.md`):
  `make migrations` depois de mudar um modelo.

## Principais endpoints (API)

| Método/Rota          | Descrição                                          |
|----------------------|----------------------------------------------------|
| `GET  /api/config/`  | Idioma, marca, versão e o projeto aberto (ou Home) |
| `GET/DELETE /api/projects/recent/` | Projetos recentes da Home / tirar da lista |
| `GET  /api/projects/cover/?path=` | Capa do card (a capa marcada; senão a 1ª foto de `raw/`) |
| `GET  /api/develop/` | Sliders, presets e camadas (`smart_select`)        |
| `POST /api/photos/<id>/segment/` | Traço do pincel → máscara do objeto    |
| `GET  /api/masks/<chave>.png` | Máscara de uma camada (overlay)           |
| `POST /api/photos/<id>/duplicate/` | Cópia virtual da foto, com os ajustes dela |
| `POST /api/photos/<id>/cover/` | Marca (`{"cover": true}`) ou desmarca a capa do evento |
| `POST /api/merges/`  | Cria o merge (`{"photos": [...]}`, a 1ª é a base)  |
| `GET/PUT/DELETE /api/merges/<id>/` | Lê, edita (área/opacidade) e desfaz o merge |
| `DELETE /api/merges/<id>/layers/<foto>/` | Tira a foto da composição (volta à filmstrip) |
| `GET  /api/merges/<id>/layers/<foto>.png?mask=` | Área alinhada sobre o preview da base |
| `POST /api/export/`  | Dispara o Exportar (grava `capa.jpg` também)       |
| `/admin/`            | Django admin público                               |
