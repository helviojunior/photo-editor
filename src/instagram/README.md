# Ajudante do Instagram (`photoe_instagram.py`)

Única parte do PhotoE que continua em Python: login (com duas etapas e o
"confirme que é você") e publicação pela API privada do app do celular
(`instagrapi`). O app C++ sobe o processo com `QProcess` — sem servidor HTTP:

```
python -I -E -s photoe_instagram.py --data-dir <~/.photoe>
```

- Sessão em `<data-dir>/instagram.json` (0600), **mesmo formato** do backend
  Django antigo (`username`, `session`, `connected_at`, `caption_template`):
  quem já estava conectado continua. A senha nunca é gravada nem logada.
- Log: stderr + `<data-dir>/logs/instagram.log` (5 MB × 3). Os loggers do
  `instagrapi`/`urllib3` ficam em `WARNING` e um filtro apaga senha/cookies.
- Fim do stdin (EOF) = o app fechou: o processo sai com código 0.

## Protocolo

JSON, **um objeto por linha**, UTF-8. O stdout é só do protocolo (o fd 1
original é reservado no boot; `print()` perdido vai para o stderr).

| Direção | Forma |
|---|---|
| pedido (stdin) | `{"id": 7, "cmd": "status", "args": {}}` |
| resposta ok | `{"id": 7, "ok": true, "result": {...}}` |
| resposta erro | `{"id": 7, "ok": false, "error": "<chave i18n>", "detail": "...", "params": {"max": 10}}` |
| evento (sem `id`) | `{"event": "login" \| "publish", ...}` |

`params` só aparece quando a chave tem parâmetro (`instagram.tooMany`,
`instagram.captionTooLong`). JSON inválido responde com `"id": null`.
Erros de protocolo: `protocol.badRequest`, `protocol.unknownCommand`.

### Comandos

| `cmd` | `args` | `result` |
|---|---|---|
| `ping` | — | `{"pong": true, "protocol": 1, "fake": false}` |
| `status` | — | `{"connected", "username", "connected_at", "caption_template" (null = padrão), "default_template", "max_photos": 10, "caption_max": 2200, "login": <login>\|null, "publishing"}` |
| `set_template` | `{"text"}` (null/vazio = padrão) | o mesmo do `status` |
| `logout` | — | o mesmo do `status` (o modelo da legenda fica) |
| `login` | `{"username", "password"}` | `<login>` (começa em `running`) |
| `code` | `{"code"}` | `<login>` |
| `cancel` | — | `{"login": <login>\|null}` |
| `publish` | `{"files": [caminhos absolutos, 1..10], "caption"}` | `{"publish_id", "total", "username"}` |

`<login>` = `{"login_id", "state", "kind", "username", "error", "detail"}`.

### Eventos

```
{"event":"login","login_id":1,"state":"running","kind":"","username":"x","error":"","detail":""}
{"event":"login","login_id":1,"state":"code","kind":"two_factor"|"challenge",...}
{"event":"login","login_id":1,"state":"done",...}
{"event":"login","login_id":1,"state":"error","error":"instagram.badCredentials","detail":""}
{"event":"login","login_id":1,"state":"cancelled",...}

{"event":"publish","publish_id":1,"state":"running","phase":"upload","total":3}
{"event":"publish","publish_id":1,"state":"done","media_id":"...","code":"Cxyz","url":"https://www.instagram.com/p/Cxyz/","username":"x"}
{"event":"publish","publish_id":1,"state":"error","error":"instagram.publishFailed","detail":"..."}
```

Um `login` novo cancela o anterior (que emite `cancelled` com o `login_id`
antigo): ignore eventos de outro `login_id`. Cancelar enquanto o pedido ao
Instagram está em voo vale na hora; o resultado que chegar depois é
descartado (a sessão não é gravada). Sem código em 600 s, o login falha.

### Chaves de erro

`instagram.missingCredentials`, `missingCode`, `noCodePending`,
`badCredentials`, `badCode`, `challenge`, `sessionExpired`, `wait`,
`suspended`, `network`, `failed`, `notConnected`, `noPhotos`, `tooMany`,
`captionTooLong`, `publishRunning`, `publishFailed` — as mesmas do backend
antigo. `detail` só vem preenchido em `failed`, `challenge` e `publishFailed`.

## Testes (sem rede)

`PHOTOE_INSTAGRAM_FAKE=1` troca o `instagrapi` por um cliente falso
(usuários `fake_2fa`/`123456`, `fake_challenge`/`654321`, `fake_bad`,
`fake_wait`, `fake_slow`; arquivo com `fail` no nome falha no envio).

```bash
docker run --rm -v "$PWD/instagram":/w -w /w python:3.12-slim \
  sh -c 'pip -q install -r requirements.txt && python -m unittest -v'
```
