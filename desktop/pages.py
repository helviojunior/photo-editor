"""Telas do proprio shell, mostradas enquanto nao ha servidor para o React.

Duas so: "abrindo o projeto" e "o servidor caiu". Todo o resto (Home, editor)
e o frontend React. HTML local via ``setHtml``, nas cores do tema escuro do
app, com o logo embutido (data URI) — nada sai para a rede. Links de acao
apontam para ``SHELL_BASE`` e sao interceptados pela ``AppPage``.
"""
import base64
import html

from desktop import paths

_STYLE = """
:root { color-scheme: dark; }
* { box-sizing: border-box; }
html, body { height: 100%; margin: 0; }
body {
  background: #09090b; color: #fafafa;
  font: 14px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Inter, Roboto, sans-serif;
  display: flex; align-items: center; justify-content: center;
  user-select: none; -webkit-user-select: none; cursor: default;
}
main { width: 100%; padding: 32px; display: flex; flex-direction: column;
       align-items: center; gap: 18px; text-align: center; }
img.logo { height: 34px; opacity: .95; }
h1 { font-size: 18px; font-weight: 600; margin: 0; }
p { margin: 0; color: #a1a1aa; max-width: 640px; }
code { color: #e4e4e7; font-size: 12px; word-break: break-all; }
.spinner { width: 28px; height: 28px; border-radius: 50%;
           border: 3px solid #27272a; border-top-color: #ef3236;
           animation: spin .8s linear infinite; }
@keyframes spin { to { transform: rotate(360deg); } }
.box { display: flex; gap: 12px; align-items: flex-start; text-align: left;
       background: rgba(239, 50, 54, .08); border: 1px solid rgba(239, 50, 54, .35);
       border-radius: 10px; padding: 14px 16px; max-width: 640px; }
.box svg { flex: none; margin-top: 2px; }
.actions { display: flex; gap: 10px; flex-wrap: wrap; justify-content: center; }
a.btn { color: #fafafa; text-decoration: none; padding: 9px 16px; border-radius: 8px;
        border: 1px solid #3f3f46; background: #18181b; min-height: 36px; }
a.btn:hover { background: #27272a; }
a.btn.primary { background: #ef3236; border-color: #ef3236; }
a.btn.primary:hover { background: #dc2626; }
"""

_ERROR_ICON = (
    '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#f87171" '
    'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'
    '<circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/>'
    '<line x1="12" y1="16" x2="12.01" y2="16"/></svg>'
)


def _logo() -> str:
    try:
        data = paths.asset('assets/logo/photoe-dark.png').read_bytes()
    except OSError:
        return ''
    return f'<img class="logo" alt="" src="data:image/png;base64,{base64.b64encode(data).decode()}">'


def _page(lang: str, body: str) -> str:
    return (f'<!doctype html><html lang="{html.escape(lang)}"><head><meta charset="utf-8">'
            f'<title>{paths.APP_NAME}</title><style>{_STYLE}</style></head>'
            f'<body><main>{_logo()}{body}</main></body></html>')


def loading(tr, project_name=None) -> str:
    title = (tr('loading.project', name=project_name) if project_name
             else tr('loading.home'))
    hint = f'<p>{html.escape(tr("loading.hint"))}</p>' if project_name else ''
    return _page(tr.language, f'<div class="spinner" role="status"></div>'
                              f'<h1>{html.escape(title)}</h1>{hint}')


def server_error(tr, log_file, has_project) -> str:
    actions = [f'<a class="btn primary" href="/retry">{html.escape(tr("error.retry"))}</a>']
    if has_project:
        actions.append(f'<a class="btn" href="/home">{html.escape(tr("error.home"))}</a>')
    actions.append(f'<a class="btn" href="/logs">{html.escape(tr("error.logs"))}</a>')
    return _page(tr.language, (
        f'<div class="box" role="alert">{_ERROR_ICON}<div>'
        f'<h1>{html.escape(tr("error.title"))}</h1>'
        f'<p>{html.escape(tr("error.text"))}</p><code>{html.escape(str(log_file))}</code>'
        f'</div></div><div class="actions">{"".join(actions)}</div>'
    ))
