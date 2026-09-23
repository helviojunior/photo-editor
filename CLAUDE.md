# Instruções do Projeto

Editor de fotos de eventos, no estilo Lightroom. As convenções abaixo são
obrigatórias e devem ser replicadas em qualquer projeto derivado deste.

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
  recortaria.
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
endpoint são abertos.

- **API:** `REST_FRAMEWORK` sem classes de autenticação e com `AllowAny` como
  permissão padrão (`core/settings.py`). Sem `SessionAuthentication`, o DRF
  também não exige CSRF nas chamadas da SPA.
- **Django admin público:** `photoeditor/middleware.py` —
  `PublicAdminMiddleware` loga toda visita a `/admin/` como o usuário padrão
  `admin`, que **não tem senha** (`set_unusable_password`). O usuário é
  garantido no boot (`startup.py:ensure_admin_user`) e, na falta dele, na
  primeira visita (`get_default_admin()`, que também reaplica `is_staff` /
  `is_superuser` caso alguém os desligue pelo próprio admin).
- **Usuário:** o `auth.User` padrão do Django — não há `AUTH_USER_MODEL` próprio.
- **Estáticos do admin:** `STATIC_URL = /django-static/` (o `/static/` é do
  build do React), servidos pelo `dj_static.Cling` em `core/wsgi.py` e
  repassados pelo nginx junto com `/admin/` (`nginx/app_locations.conf`).

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

### 11. Favicon e logo remotos: copiados de `../sec_face`
Favicon e logo são servidos remotamente de `media.sec4us.com.br`:

- `public/index.html` referencia
  `https://media.sec4us.com.br/icon/favicon.ico|.png?ts=%REACT_APP_BUILD_TS%`.
- `src/lib/brand.js` resolve nome, logo, logo escuro, favicon e e-mail de
  contato a partir de `REACT_APP_BRAND_*` / `REACT_APP_MEDIA_BASE`.
- O logo do rodapé dos e-mails vem da mesma origem (setting `BRAND_EMAIL_LOGO`).

### 11.1. Cache-busting `?ts=` em todo objeto estático
Todo asset estático carrega o carimbo do build: `?ts=<REACT_APP_BUILD_TS>`.

- `frontend/craco.config.js` fixa `REACT_APP_BUILD_TS` uma vez por build (mesmo
  valor em `start` e `build`) e interpola `%REACT_APP_BUILD_TS%` nos estáticos
  emitidos (`manifest.json`, `asset-manifest.json`).
- `src/lib/asset.js` expõe `BUILD_TS`, `withTs(url)` e `asset(path)` — use-os
  para **qualquer** URL de asset (imagens, sons, PDFs), local ou remota.
- Os Dockerfiles carimbam `REACT_APP_BUILD_TS=$(date -u +%Y%m%d%H%M%S)` no
  build; a CI pode sobrescrever exportando a variável.

## Configuração e banco

### 12. `.env` único
Existe **um único `.env` na raiz** do repositório, consumido pelos
`docker-compose*.yml` (`env_file` + interpolação) e pelo backend Django
(`core/settings.py` carrega a raiz; `backend/.env` só como fallback legado).
As variáveis de build do frontend (`REACT_APP_*`) saem desse mesmo arquivo e
chegam ao React como build args.

- **Proibido:** `.env` separado por serviço (não existe `frontend/.env`),
  valores duplicados entre compose e backend, ou segredo direto no compose.
- **Única exceção:** `<DATA_DIR>/.env`, gerado no primeiro boot com os segredos
  do próprio processo (`SECRET_KEY`, chave RSA) — é estado, não configuração, e
  também é carregado pelas settings.
- **Nunca versionar o `.env`** (nem qualquer `.env` local com segredos). Só o
  `.env.example` — template sem valores sensíveis — vai para o git; o `.env`
  carrega segredos reais (banco, senha SMTP) e commitá-lo
  vaza esses dados no histórico. Manter `.env` no `.gitignore` (já está); nunca
  `git add .env` nem `git add -A` sem confirmar que o `.env` continua ignorado.

### 12.1. Banco: SQLite dentro da pasta do projeto, `DEBUG=False` por padrão
Cada evento é uma pasta de projeto no host, montada em `/project` no backend
(`PROJECT_DIR` no `.env` → volume nos `docker-compose*.yml`):

```
/project/raw/            originais (somente JPEG, nunca alterados)
/project/project_data/   db.sqlite3 + caches gerados
/project/deleted/        fotos excluídas (movidas, nunca apagadas)
/project/publicar/       saída do Exportar
```

- O banco é **sempre** o SQLite em `project_data/db.sqlite3`: catálogo e
  ajustes acompanham as fotos do evento. Não há PostgreSQL nem outro banco.
- **Sem `/project` montado, `core/settings.py` levanta `ImproperlyConfigured`**
  — nunca cai para um banco em outro lugar, o que "perderia" o catálogo na
  execução seguinte. Os caminhos vêm das settings (`PROJECT_ROOT`, `RAW_DIR`,
  `PROJECT_DATA_DIR`, `DELETED_DIR`, `PUBLISH_DIR`); nada de caminho montado à
  mão no código.
- `project_data/` nasce nas settings (o SQLite precisa dela antes do
  `migrate`); `deleted/` e `publicar/` no init da app
  (`startup.py:ensure_project_dirs`). `raw/` **não** é criada: sem ela o log
  mostra um erro claro e a app continua de pé.
- O entrypoint roda `makemigrations` + `migrate` a cada boot.
- `DEBUG` tem default `False`; ligar exige `DEBUG=True` explícito no ambiente.
- O entrypoint não cria conta nenhuma: o `admin` do Django admin público é
  garantido pelo init da app (regra 5).

### 12.3. Log do backend sai no stdout do container
Todo log do backend vai para o **stdout do processo** — é lá que o Docker
coleta (`docker compose logs -f backend`). Log que não aparece no `docker logs`
não existe.

- **Proibido:** `SysLogHandler`, arquivo de log dentro do container, ou handler
  montado à mão no módulo (`if os.isatty(0): ... else: ...`). Dentro do
  container não há TTY nem `/dev/log`, e o syslog engole a mensagem.
  Também proibido `print()` para diagnóstico — use `logging`.
- **Como aplicar:** a configuração é única, em `core/settings.py` (`LOGGING`,
  dictConfig) com um handler `console` para `sys.stdout` **sem** o filtro
  `require_debug_true` — o padrão do Django silencia tudo com `DEBUG=False`,
  que é o modo normal deste projeto. Nos módulos, apenas
  `log = logging.getLogger(__name__)`; nada de `addHandler`/`basicConfig`.
- **Níveis por ambiente:** `LOG_LEVEL` (aplicação + root, default `INFO`),
  `DJANGO_LOG_LEVEL` (loggers do Django) e `SQL_LOG_LEVEL`
  (`django.db.backends`, default `WARNING`).
- **Sem buffer:** o `backend/Dockerfile` define `PYTHONUNBUFFERED=1`; sob uwsgi
  o stdout fica em buffer de bloco e o log atrasa ou se perde no crash.

### 13. Migrations incrementais
O banco de cada projeto de fotos (`project_data/db.sqlite3`) é dado real e
persiste entre versões. Por isso as migrations são **incrementais**
(`0001_initial.py`, `0002_...`, …) e versionadas junto com a mudança de modelo.

- **Proibido:** apagar ou regenerar uma migration já commitada — um banco que
  já a aplicou nunca receberia as mudanças.
- **Como aplicar:** ao mudar um modelo, rode `makemigrations photoeditor` (via
  Docker, regra 15.1) e commite a migration nova no mesmo commit. O entrypoint
  roda `makemigrations` + `migrate` no boot, mas a migration gerada ali vive só
  no container — a versionada é a que vale. `makemigrations --check` deve
  reportar "No changes detected".

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
A máquina do desenvolvedor não tem todos os runtimes instalados (Node/npm, por
exemplo). Sempre que for preciso conferir um comando, uma sintaxe, uma versão de
lib ou rodar um lint/build de um runtime ausente, **execute via Docker** em vez
de instalar a ferramenta no host ou desistir da verificação.

- **Como aplicar:** rode um container descartável montando o diretório do
  projeto, na mesma imagem usada pelo build (`node:20-alpine` para o frontend,
  conforme `frontend/Dockerfile`):

  ```bash
  docker run --rm -v "$PWD/frontend":/app -w /app node:20-alpine node -e '...'
  docker run --rm -v "$PWD/frontend":/app -w /app node:20-alpine npx eslint src
  ```

  O mesmo vale para qualquer outro runtime (Python, psql, etc.): imagem oficial,
  `--rm`, volume no projeto. Com os serviços de pé, `docker compose exec` também
  serve para checar algo dentro do container.

### 16. Commits vão direto na `main`
O fluxo é trunk-based: o histórico de `git@gitlab.com:saas-sec4us/photo-editor.git`
é linear na `main`.

- **Proibido:** criar branch de feature ou abrir merge request para publicar uma
  alteração.
- **Como aplicar:** quando o usuário pedir para commitar/publicar, `git add` +
  `git commit` + `git push origin main`, sem `git checkout -b`. Commitar apenas
  quando o usuário pedir, usar a identidade configurada no git (sem `--author`)
  e manter as mensagens em português, como o resto do histórico.

### 17. Versão sobe a cada commit
O arquivo `VERSION` na raiz carrega a versão do deploy, no formato `X.Y.Z`
começando em `1.0.0`. **Cada commit incrementa em um**, e cada numerador vai
até 999 antes de virar o de cima (`1.0.999` → `1.1.0` → … → `1.999.999` →
`2.0.0`).

- **Não é semver:** o último número não significa "correção", significa "mais
  um commit". O que a versão responde é *"qual commit está rodando no deploy"*,
  e para isso um contador contínuo serve melhor do que decidir, a cada commit,
  se aquilo foi feature ou fix.
- **Como aplicar:** rode `./bump-version.sh` ANTES de commitar e inclua o
  `VERSION` no mesmo commit — a versão tem de apontar para o commit que a
  carrega, não para o anterior.
- O valor chega ao frontend como `REACT_APP_VERSION` (build arg, via
  `APP_VERSION` no compose) e ao backend por `core.settings.VERSION`, que lê a
  mesma fonte. Uma string fixa no código envelhece no primeiro commit e passa a
  mentir sobre o que está rodando.
