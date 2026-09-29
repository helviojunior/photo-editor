# Instruções do Projeto

Editor de fotos de eventos, no estilo Lightroom, distribuído como **app
desktop** (Windows, macOS, Linux): Python embarcado + Chromium embarcado (Qt
WebEngine) mostrando o frontend React servido por um Django local — sem Docker
em tempo de execução (regra 18). As convenções abaixo são obrigatórias e devem
ser replicadas em qualquer projeto derivado deste.

O sistema é **100% público e não autenticado** (regra 5): não existe login,
conta, Company nem permissionamento — nada de reintroduzir essas camadas sem
pedido explícito.

## UI / UX

### 1. Modais próprios — nunca os do navegador
Toda confirmação, alerta, aviso ou mensagem ao usuário deve usar os componentes
de modal do próprio sistema, consistentes com o design da aplicação.

- **Proibido:** `window.alert`, `window.confirm`, `window.prompt` ou qualquer
  API que renderize um popup nativo do navegador.
- **Como aplicar:** use a API do sistema, já disponível no frontend:
  - `src/components/ui/modal.jsx` — componente `Modal` base (portal, backdrop,
    ESC, botão de fechar).
  - `src/contexts/DialogContext.jsx` — `DialogProvider` (montado em `App.js`) e
    o hook `useDialog()`, que expõe `confirm()` e `alert()` retornando Promise:

    ```jsx
    const { confirm, alert } = useDialog();

    const ok = await confirm({
      title: "Excluir foto?",
      description: <>Excluir <strong>{photo.name}</strong>?</>,
      variant: "danger",          // danger | warning | success | info
      confirmLabel: "Excluir",
      onConfirm: async () => api.delete(`/api/photos/${photo.id}/`),
    });

    await alert({ title: "Erro ao salvar", variant: "danger" });
    ```
  - `onConfirm` mantém o modal aberto (com loading) enquanto a ação executa; se
    lançar erro, o modal permanece aberto para nova tentativa.

### 2. Telas e formulários ocupam 100% da área disponível
Toda tela e todo formulário devem preencher a largura total da região de
conteúdo em que estão inseridos.

- **Proibido:** "cards" estreitos e centralizados que deixam grandes margens
  laterais vazias; `max-width` fixo em containers de formulário.
- **Como aplicar:** o wrapper da página (`w-full`), o `Card`, o `<form>` e seus
  campos (`Input`, `select`, `textarea`) ocupam a largura disponível; para
  agrupar campos use grid responsivo (`grid gap-4 md:grid-cols-2`) em vez de
  limitar a largura do container.
- Toda tela vive dentro do `AppLayout` e segue a regra dos 100% — não há
  tela de login com card centralizado.

### 2.1. Cor: vermelho é acento, esmeralda é confirmação, erro tem forma própria
O vermelho da marca (escala `brand-*` no `tailwind.config.js`, derivada do
`BRAND_COLOR`) é a cor de **acento**: item ativo do menu, ícone de seção,
seleção, destaque informativo.

- **Confirmação/sucesso continua em `emerald-*`** — o ✓ que afirma algo
  positivo, o diálogo de sucesso, a validação atendida.
- **Erro nunca é só texto colorido.** Use `components/ui/form-error.jsx`
  (`FormError` / `FormErrors`): caixa com fundo preenchido, borda e **ícone
  obrigatório**.

> **Por quê:** o vermelho da marca fica a **ΔE 7,6** do `red-500` — abaixo de
> ~10 o olho trata como a mesma cor. Sem uma forma que os separe, "confirmado"
> e "falhou" ficariam idênticos. O ícone também é o que carrega o significado
> para quem não distingue vermelho de verde (~8% dos homens).

> **Ao forkar:** troque o `#ef3236` da escala pela cor da marca do projeto e
> regenere os dez tons pela mesma regra (mesma matiz, luminosidade variando).

### 2.2. Booleano é sempre o Toggle
Todo campo booleano usa o `Toggle` de `src/components/ui/toggle.jsx` — é o
controle padrão de checkbox de todo o sistema.

- **Proibido:** `<input type="checkbox">` nativo e toggles reimplementados
  dentro de cada tela (geram cores, tamanhos e acessibilidade divergentes).
- **Como aplicar:** `import { Toggle } from "components/ui/toggle";` —
  `onChange` recebe **o novo booleano**, não o evento
  (`onChange={(v) => setCampo(v)}`) e o rótulo vai na prop `label`, já clicável.
  Para rótulo com descrição, use o `ToggleField` do mesmo módulo.

### 2.3. Seleção em lista é o círculo com ✓, não o Toggle
Marcar linhas de uma listagem usa o `SelectionCheck` de
`src/components/ui/selection-check.jsx` (padrão do `../sec_chall`).

- **Não confunda com o `Toggle` (regra 2.2).** O Toggle é para um estado que a
  linha *tem* e que a pessoa liga e desliga ("conta bloqueada", "exigir
  maiúscula"). O `SelectionCheck` diz "esta linha está no conjunto que vou
  operar agora" — outra pergunta. Usar o mesmo controle para as duas faz a
  lista parecer cheia de interruptores pendurados em cada registro.
- **Quem recebe o clique é a LINHA inteira**, não o círculo: área maior, e o
  mesmo alvo que abre o detalhe quando não se está selecionando. Por isso o
  `SelectionCheck` é um `<span aria-hidden>`, e o `role="checkbox"` +
  `aria-checked` vão no elemento da linha.
- **A seleção só aparece quando alguém pede.** A coluna de marcas entra em
  cena ao entrar no modo (um botão do tipo "Unificar contas" liga, "Sair da
  seleção" desliga) e sai junto — controles em cada linha, o tempo todo, para
  uma ação que quase nunca se usa, são ruído permanente.

> Nenhuma listagem do baseline usa seleção ainda; o componente existe para que
> a primeira que precisar não invente outro controle.

### 2.4. Mobile não é um ajuste, é parte da tela
Toda tela nasce funcionando no celular.

- **Navegação:** `components/layout/Sidebar.jsx` — `useIsMobile()` (via
  `matchMedia`, não `resize`) e `SidebarShell`, que devolve o `<aside>` no
  desktop e uma **gaveta sobreposta** abaixo de `lg:`. A gaveta vai para um
  portal no `body`: dentro da árvore, um ancestral com `overflow-hidden` a
  recortaria. O editor **não** usa barra lateral (TODO 3.4: o espaço é das
  fotos) — o `AppLayout` é só cabeçalho + conteúdo; o `SidebarShell` fica para
  quando houver mais de uma tela a navegar.
- **Altura:** `h-dvh`, nunca `h-screen`. `100vh` no iOS Safari conta a barra de
  URL retrátil e esconde o rodapé do conteúdo.
- **Alvo de toque:** a classe `.touch-target` (`index.css`) dá 44px de altura
  mínima **apenas** em `@media (pointer: coarse)` — o problema é do dedo, e a
  correção fica onde o problema está. Já aplicada no `Button`, `Input`,
  `Toggle`, `select` e nos botões de ícone das listas.
- **Como validar:** sem navegador na máquina, rode um Playwright descartável em
  Docker (regra 15.1) nos viewports 360×640, 390×844 e 768×1024 e confira que
  `document.documentElement.scrollWidth` não passa de `clientWidth`.

### 3. Detalhe de objeto abre em nova janela
Toda visualização de detalhe de um objeto/entidade deve ser uma nova janela
(rota/página própria), nunca apenas um modal sobreposto.

- **Como aplicar:** detalhe de registro = rota dedicada (ex.: `/entidade/:id`),
  navegável, com URL própria e possibilidade de abrir em nova aba. Modais ficam
  reservados para confirmações e mensagens curtas — não para exibir detalhes.
- Nenhuma entidade do editor tem detalhe ainda; a primeira segue o padrão
  lista → `/entidade/:id` (detalhe) → `/entidade/:id/edit` (edição).

## Internacionalização (i18n)

### 4. Todo o sistema é multi-language
Nenhum texto visível ao usuário pode ser hard-coded. Idiomas suportados hoje:
**EN** e **PT-BR** (a lista deve ser extensível sem alterar componentes).

- **EN é o idioma padrão de todo o sistema** e **o fallback é sempre o inglês**:
  chave sem tradução no idioma ativo cai para EN, nunca para outro idioma nem
  para a chave crua. O texto do fallback nas chamadas (`t("chave", "fallback")`)
  é escrito em inglês.

- **Escopo:** frontend (telas, modais, validações), backend (mensagens de erro
  da API, textos de admin) e **e-mails**.
- **Como aplicar (frontend):** `src/i18n/` — `I18nProvider` (montado em
  `App.js`) e o hook `useI18n()` com `t("chave", "fallback")` e
  `tf("chave", { param })`; catálogos em `src/i18n/locales.js`.
- **Como aplicar (backend):** `photoeditor/i18n.py` — `translate(key, lang)`,
  `tr(request, key)` e `language_for_request(request)`; catálogos EN/PT-BR no
  mesmo arquivo.

### 5. Sistema público, sem autenticação
Não há login, contas, Company, papéis nem capabilities. Toda tela e todo
endpoint são abertos **para quem usa o app**.

- **API:** `REST_FRAMEWORK` sem classes de autenticação e com `AllowAny` como
  permissão padrão (`core/settings.py`). Sem `SessionAuthentication`, o DRF
  também não exige CSRF nas chamadas da SPA.
- **Trava de transporte, não de usuário:** o servidor local escuta em
  `127.0.0.1`, aceita só Host `127.0.0.1`/`localhost` (DNS rebinding) e o
  `AppTokenMiddleware` (`photoeditor/middleware.py`) recusa com 403 toda
  requisição sem o token sorteado pelo shell a cada execução — entregue ao
  navegador embarcado como cookie `HttpOnly`/`SameSite=Strict`. Sem isso,
  qualquer site aberto no navegador comum da pessoa chamaria a API. Não
  confundir com autenticação: o token não identifica ninguém.
- **Django admin público:** `photoeditor/middleware.py` —
  `PublicAdminMiddleware` loga toda visita a `/admin/` como o usuário padrão
  `admin`, que **não tem senha** (`set_unusable_password`). O usuário é
  garantido no boot (`startup.py:ensure_admin_user`) e, na falta dele, na
  primeira visita (`get_default_admin()`, que também reaplica `is_staff` /
  `is_superuser` caso alguém os desligue pelo próprio admin).
- **Usuário:** o `auth.User` padrão do Django — não há `AUTH_USER_MODEL` próprio.
- **Estáticos:** `STATIC_URL = /django-static/` (o `/static/` é do build do
  React). O WhiteNoise serve os dois: os do admin direto das apps
  (`WHITENOISE_USE_FINDERS`, sem `collectstatic`) e o build do React na raiz
  (`WHITENOISE_ROOT`); rota que não é arquivo nem API cai no `index.html`
  (`views/spa.py`).

### 6. Idioma: cookie → navegador → padrão do sistema
Sem conta onde guardar preferência, o idioma é resolvido em três degraus
(`frontend/src/lib/language.js`):

```
cookie  ->  navegador  ->  padrão do sistema (DEFAULT_LANGUAGE)
```

- O **cookie** (`photoeditor_ln`) é escrito pelo **frontend** ao trocar o
  idioma (`writeLanguageCookie`, chamado por `setLanguage`) e vem primeiro
  porque é escolha explícita de quem usa o sistema; o navegador é a
  configuração do *aparelho*, e muita gente usa o sistema operacional em inglês
  e prefere ler em português.
- O **padrão do sistema** chega tarde, na resposta do `/api/config/`
  (`default_language`), depois de a tela já estar desenhada. Por isso o estado
  guarda a **origem** do idioma e só o adota quando nada respondeu antes —
  aplicá-lo sempre atropelaria uma preferência real.
- **Shell desktop:** menus e diálogos nativos (`desktop/i18n.py`) seguem o
  idioma escolhido no app observando o cookie no perfil do navegador
  embarcado; antes da primeira escolha, o idioma do SO.
- **Backend:** respostas seguem a mesma ordem
  (`i18n.language_for_request`: cookie → `Accept-Language` → EN); e-mails saem
  no idioma que quem envia informar ao `mailer`. **Sem idioma ou idioma não
  suportado → EN.**
- O nome do cookie está repetido no backend (`i18n.py`) e no frontend
  (`lib/language.js`) de propósito: o frontend precisa dele antes de qualquer
  chamada à API. **Ao forkar, renomeie nos dois** — divergir faz o cookie ser
  escrito com um nome e procurado com outro, e o sintoma é o idioma
  simplesmente não persistir, sem erro nenhum.

## Comunicação e identidade visual

### 10. E-mail é template, não string em Python
O layout de todo e-mail mora em `photoeditor/templates/email/`:
`_base.html` (shell da marca: barra, card branco, rodapé escuro, preheader
oculto), `_logo.html` e um template por mensagem. Quem renderiza é
`services/mailer.py: render()`, que injeta o que toda mensagem tem — assunto,
preheader, rodapé, logo.

- **Proibido:** montar HTML por concatenação de strings em Python. HTML dentro
  de aspas não tem realce, não escapa nada por padrão, e só se vê renderizado
  depois de enviar.
- **Comentário de template é `{% comment %}`, NUNCA `{# #}`.** O `{# #}` do
  Django é de **uma linha só** (o lexer não usa `re.DOTALL`): um bloco de
  várias linhas entre `{#` e `#}` **vaza como texto no corpo do e-mail**, e o
  destinatário lê o comentário.
- Cor, site e logo saem das settings `BRAND_*`; o idioma é o informado por
  quem envia (regra 6).

### 11. Favicon, logo e fonte no build — o app funciona offline
Nada da interface depende de rede: favicon (`public/favicon.ico|.png`), logo e
a fonte Inter (`@fontsource/inter`, importada em `src/index.js` — nada de
Google Fonts) vão no próprio build. O logo do PhotoE foi baixado de
`media.sec4us.com.br`:

- `public/index.html` referencia `%PUBLIC_URL%/favicon.ico|.png?ts=%REACT_APP_BUILD_TS%`.
- Logo: `public/assets/logo/photoe-light.png` (tema claro) e
  `photoe-dark.png` (tema escuro — texto branco), originais em
  `https://media.sec4us.com.br/logo/photoe-{light,dark}.png`. Para trocar,
  substitua os arquivos (mesmo nome) ou defina `REACT_APP_BRAND_LOGO` /
  `REACT_APP_BRAND_LOGO_DARK` no `.env`; vazias, valem os arquivos locais.
- `src/lib/brand.js` resolve nome, logo, logo escuro, favicon e e-mail de
  contato a partir de `REACT_APP_BRAND_*`.
- O logo do rodapé dos e-mails vem da mesma origem (setting `BRAND_EMAIL_LOGO`).

### 11.1. Cache-busting `?ts=` em todo objeto estático
Todo asset estático carrega o carimbo do build: `?ts=<REACT_APP_BUILD_TS>`.

- `frontend/craco.config.js` fixa `REACT_APP_BUILD_TS` uma vez por build (mesmo
  valor em `start` e `build`) e interpola `%REACT_APP_BUILD_TS%` nos estáticos
  emitidos (`manifest.json`, `asset-manifest.json`).
- `src/lib/asset.js` expõe `BUILD_TS`, `withTs(url)` e `asset(path)` — use-os
  para **qualquer** URL de asset (imagens, sons, PDFs), local ou remota.
- O builder (`tools/build.py frontend`) carimba
  `REACT_APP_BUILD_TS=<UTC %Y%m%d%H%M%S>` e `REACT_APP_VERSION` (regra 17); a CI pode
  sobrescrever exportando a variável.

## Configuração e banco

### 12. `.env` só para desenvolvimento
O app instalado **não lê `.env`**: o shell passa ao servidor local tudo o que
ele precisa por variável de ambiente (`desktop/server.py`). Existe no máximo
**um `.env` na raiz**, opcional, para sobrescrever defaults ao rodar em
desenvolvimento (`core/settings.py` o carrega se existir).

- **Proibido:** `.env` separado por serviço, ou configuração que o app
  instalado precise ler de um `.env` (ele não existe na máquina de quem usa).
- **Única exceção:** `~/.photoe/.env`, gerado no primeiro uso com os segredos
  da máquina (`SECRET_KEY`, `core/secret_key.py`) — é estado, não
  configuração.
- **Nunca versionar o `.env`.** Só o `.env.example` vai para o git; manter
  `.env` no `.gitignore` (já está); nunca `git add .env` nem `git add -A` sem
  confirmar que o `.env` continua ignorado.

### 12.1. Dois bancos SQLite: o do evento e o do app; `DEBUG=False` por padrão
Cada evento é uma pasta de projeto que a pessoa escolhe no app (Home, menu
Arquivo ou linha de comando); o shell a repassa ao servidor em `PROJECT_ROOT`:

```
<projeto>/raw/            originais (somente JPEG, nunca alterados)
<projeto>/project_data/   db.sqlite3 + caches gerados + trava de "já aberto"
<projeto>/deleted/        fotos excluídas (movidas, nunca apagadas)
<projeto>/publicar/       saída do Exportar
```

- **Catálogo e ajustes** ficam **sempre** no SQLite do evento,
  `project_data/db.sqlite3`: acompanham as fotos. Não há PostgreSQL.
- **O que é da máquina** fica em `~/.photoe/` (igual nos três SOs, também no
  Windows): `photoe.db` (lista de projetos da Home), `.env` gerado, perfil do
  navegador (`webengine/`), capas dos cards (`covers/`) e `logs/`. O
  `photoe.db` é acessado com `sqlite3` puro
  (`photoeditor/services/recent_projects.py`), porque o shell (sem Django)
  também o usa; o esquema migra por `PRAGMA user_version`, de forma
  incremental como a regra 13.
- **Sem `PROJECT_ROOT`, o servidor sobe em modo Home** (`HOME_MODE`): só as
  rotas da tela inicial (`photoeditor/urls.py`) e banco em memória.
  `PROJECT_ROOT` apontando para pasta inexistente levanta
  `ImproperlyConfigured` — nunca cai para um banco em outro lugar. Os
  caminhos vêm das settings (`PROJECT_ROOT`, `RAW_DIR`, `PROJECT_DATA_DIR`,
  `DELETED_DIR`, `PUBLISH_DIR`, `APP_DB`); nada de caminho montado à mão.
- `project_data/` nasce nas settings; `deleted/` e `publicar/` no boot
  (`startup.py:ensure_project_dirs`). `raw/` só é criada pelo shell, **depois
  de perguntar** — criada calada, esconderia a escolha da pasta errada.
- Ao abrir o projeto o servidor roda **só `migrate`** (nunca
  `makemigrations`: escreveria migration na instalação) e então o boot
  (`on_startup(force=True)`).
- `DEBUG` tem default `False`; ligar exige `DEBUG=True` (ou `--debug` no app).

### 12.3. Log vai para `~/.photoe/logs/`
O app instalado não tem terminal: log que não vai para arquivo não existe.
Shell e servidor escrevem cada um no seu arquivo rotativo —
`~/.photoe/logs/desktop.log` e `server.log` — e, quando há terminal (dev),
também nele. O menu Ajuda → Abrir Pasta de Logs leva até lá.

- **Proibido:** `SysLogHandler`, handler montado à mão no módulo
  (`if os.isatty(0): ... else: ...`) ou `print()` para diagnóstico — use
  `logging`.
- **Como aplicar:** a configuração do servidor é única, em
  `core/settings.py` (`LOGGING`, dictConfig): handler de arquivo quando o
  shell passa `LOG_FILE`, console só se houver `sys.stdout`, **sem** o filtro
  `require_debug_true` (o padrão do Django silencia tudo com `DEBUG=False`).
  O shell configura o dele em `desktop/app.py:setup_logging`. Nos módulos,
  apenas `log = logging.getLogger(__name__)`.
- **Níveis:** `LOG_LEVEL` (aplicação + root, default `INFO`),
  `DJANGO_LOG_LEVEL` e `SQL_LOG_LEVEL` (`django.db.backends`, default
  `WARNING`).

### 13. Migrations incrementais
O banco de cada projeto de fotos (`project_data/db.sqlite3`) é dado real e
persiste entre versões. Por isso as migrations são **incrementais**
(`0001_initial.py`, `0002_...`, …) e versionadas junto com a mudança de modelo.

- **Proibido:** apagar ou regenerar uma migration já commitada — um banco que
  já a aplicou nunca receberia as mudanças.
- **Como aplicar:** ao mudar um modelo, rode `makemigrations photoeditor` com o
  Python embarcado (`.runtime/<alvo>/python/bin/python3 backend/manage.py
  makemigrations photoeditor`, com `PROJECT_ROOT` apontando para uma pasta de
  teste) e commite a migration nova no mesmo commit. O app só roda `migrate`:
  migration que não estiver versionada nunca chega a quem usa.
  `makemigrations --check` deve reportar "No changes detected".

## Convenções de código e versionamento

### 15. Identificadores de código sempre em inglês
Como este é um baseline reutilizável, **todo nome de variável, valor padrão,
chave de configuração, env var, função e campo de modelo é em inglês**.
Comentários e textos de UI podem seguir o idioma local; identificadores de
código, não.

- **Proibido:** misturar português em nomes de símbolos ou em valores padrão de
  config.
- **Como aplicar:** use nomes em inglês (`FORCE_TLS`, `REAL_IP_FROM`,
  `USE_REAL_IP`, `language`, `is_published`…). Identificadores em inglês
  mantêm consistência e portabilidade entre forks.

### 15.1. Ferramenta não instalada na máquina? Use Docker
Docker é ferramenta de **desenvolvimento e build**, nunca de execução do app.
A máquina do desenvolvedor não tem todos os runtimes instalados (yarn, por
exemplo). Sempre que for preciso conferir um comando, uma sintaxe, uma versão de
lib ou rodar um lint/build de um runtime ausente, **execute via Docker** em vez
de instalar a ferramenta no host ou desistir da verificação. O Python do
projeto é o embarcado (`make runtime` → `.runtime/`), não o
do sistema.

- **Como aplicar:** rode um container descartável montando o diretório do
  projeto (`node:20-alpine` para o frontend; o builder completo é o
  `tools/Dockerfile`):

  ```bash
  docker run --rm -v "$PWD/frontend":/app -w /app node:20-alpine node -e '...'
  docker run --rm -v "$PWD/frontend":/app -w /app node:20-alpine npx eslint src
  ```

  O mesmo vale para qualquer outro runtime (Python, psql, etc.): imagem oficial,
  `--rm`, volume no projeto.

### 16. Commits vão direto na `main`
O fluxo é trunk-based: o histórico de `git@gitlab.com:saas-sec4us/photo-editor.git`
é linear na `main`.

- **Proibido:** criar branch de feature ou abrir merge request para publicar uma
  alteração.
- **Como aplicar:** quando o usuário pedir para commitar/publicar, `git add` +
  `git commit` + `git push origin main`, sem `git checkout -b`. Commitar apenas
  quando o usuário pedir, usar a identidade configurada no git (sem `--author`)
  e manter as mensagens em português, como o resto do histórico.

### 17. O GitHub manda na versão
A versão **não mora no repositório**: releases e mudanças de numeração só
acontecem no GitHub. Publicar uma Release com a tag `vX.Y.Z` (ou `X.Y.Z`) é o
que cria uma versão; entre uma Release e outra, todo build é "a última Release
+ o canal".

| Canal | Onde | Número | O app exibe | Pacote |
|---|---|---|---|---|
| `release` | CI, ao publicar a Release | a tag | `1.2.3` | `PhotoEditor-1.2.3-<alvo>` |
| `dev` | máquina local (`make`) | última Release | `1.2.3-dev+<commit>` | `PhotoEditor-dev-v1.2.3-<alvo>` |

- **Proibido:** versionar o arquivo `VERSION`, subir número "na mão" num
  commit ou gravar versão fixa no código. Não existe mais bump por commit.
- **Quem resolve:** `tools/build.py` (`version()`, `version_label()`) — a
  última Release vem da API do GitHub (cache de 24 h; sem Release, `0.0.0`);
  `PHOTOEDITOR_VERSION` / `PHOTOEDITOR_CHANNEL` sobrescrevem (a CI usa). Ele
  gera o `VERSION` da raiz (ignorado pelo git), de onde leem o frontend
  (`REACT_APP_VERSION`), o shell (`desktop/paths.version()`) e o backend
  (`core.settings.VERSION`); `make version`/`tools/build.py version` mostra.
- **Formato:** `X.Y.Z` com X até 255 e Y, Z até 999 — o limite do MSI
  (`msi_version`). O `.app` e o `.msi` recebem só o número; o sufixo
  `-dev+<commit>` é para exibição e nome de arquivo.

## App desktop

### 18. Shell nativo, navegador endurecido, builder em Docker
O app são dois processos do mesmo Python embarcado: o **shell**
(`desktop/app.py`, `window.py`) — janela, menus, escolha de projeto — e o
**servidor** (`desktop/server.py`) — Django + waitress em `127.0.0.1`, um por
projeto aberto (trocar de projeto derruba um e sobe outro; sem projeto, modo
Home).

- **O navegador não pode parecer navegador** (`desktop/browser.py`): sem
  barra de endereço, abas, menu de contexto, DevTools (só com `--devtools`),
  página de erro do Chromium, plugins ou permissões de página. Navegação
  fica presa à origem do servidor; link externo vai para o navegador do SO;
  outros esquemas são barrados. Não afrouxe nada disso sem pedido explícito.
- **O que só o SO faz, o React pede navegando para `/__desktop__/<ação>`**
  (`frontend/src/lib/desktop.js`); o shell intercepta no
  `acceptNavigationRequest`. A ação **tem de rodar fora desse callback**
  (`QTimer.singleShot(0, …)`, já em `AppPage._dispatch`): `setHtml`/`setUrl`
  ou parar o servidor lá dentro reentra no Chromium e **derruba o app**
  (`EXC_BREAKPOINT` em `WebContentsAdapter::setContent`). Tela que usa essas
  ações confere `config.desktop` antes de oferecê-las.
- **Nada no shell é texto fixo** (regra 4): `desktop/i18n.py`. Diálogos do
  shell são nativos do Qt (não do navegador) — a regra 1 vale para o
  frontend.
- **Toda escrita vai para `~/.photoe/` ou para a pasta do projeto**, nunca
  para a instalação (`<instalação>/app` pode ser somente leitura).
- **Dependência Python só com wheel binária para os cinco alvos**
  (macOS arm64/x64, Windows x64, Linux x64/arm64). Windows ARM64 fica de fora
  enquanto o OpenCV não tiver wheel `win_arm64`. O builder
  (`make dist` → `tools/build.py dist` no container) monta todas num único
  container Linux com `pip --platform … --only-binary=:all:` — um pacote só
  com sdist quebra o build. Ao adicionar uma, confira as tags no PyPI: elas
  definem o SO mínimo do pacote (tabela no ARCHITECTURE.md).
- **Versões fixas no builder:** CPython (`PBS_RELEASE`/`PYTHON_VERSION`) e o
  modelo SAM (revisão + sha256) em `tools/build.py`; imagem base
  (`ubuntu:noble-<data>`) e revisão do libdmg-hfsplus no `tools/Dockerfile`.
  Trocar é decisão explícita, nunca efeito colateral de um build.
- **Instaladores saem do mesmo container** (`tools/packaging/`): `.dmg` no
  macOS, `.msi` no Windows, `.tar.gz` no Linux. Arte (ícones, fundo do DMG)
  é desenhada em código a partir do logo — não versione PNG/ICNS/ICO pronto.
- **CI no GitHub:** só o `release.yml` — ao publicar uma Release, gera os
  instaladores (pelo `package.yml`) e os anexa a ela. Não há build a cada
  push. Versão conforme a regra 17. Passo novo de empacotamento entra no `Makefile` — os
  workflows só chamam o `make`.
- **Nunca troque os GUIDs de `tools/packaging/windows.py`** (`UpgradeCode` e
  componentes dos atalhos) nem o `BUNDLE_ID` do `.app`: é por eles que o
  Windows e o macOS reconhecem a versão nova como o mesmo programa.
- **O `Makefile` é a base de compilação** (`make` = `make dist`; `make help`
  lista os alvos): novo
  passo de build, teste ou empacotamento entra como alvo nele, não como
  script solto em `tools/`.
- **Como validar:** `make test`; e `make run ARGS="--screenshot /tmp/x.png"`
  abre o app, fotografa a primeira tela carregada e sai — serve de teste de
  fumaça sem ninguém olhando a janela.

