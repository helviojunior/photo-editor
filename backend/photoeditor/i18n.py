"""Traducao de textos voltados ao usuario (API e e-mails).

Regras do baseline:

* **EN e o idioma padrao e o fallback**: chave sem traducao no idioma ativo cai
  para EN — nunca para outro idioma nem para a chave crua (se a chave tambem
  nao existir em EN, devolve o ``default`` recebido, e so entao a chave).
* O sistema e publico e nao autenticado: o idioma vem da requisicao
  (cookie -> Accept-Language -> EN), ver ``language_for_request``.

Catalogos sao dicionarios planos com chaves em ``dominio.item``. Toda chave
adicionada em PT-BR precisa existir em EN.
"""

DEFAULT_LANGUAGE = 'en'
SUPPORTED_LANGUAGES = ('en', 'pt-br')

CATALOGS = {
    'en': {
        'error.notFound': 'Not found.',
        'error.photoFileMissing': 'The file {name} is no longer where the catalog expected it.',
        'error.restoreConflict': 'Cannot restore {name}: there is already a file with that name in raw/.',

        # E-mails
        'email.greeting': 'Hello, {name},',
        'email.footer.copyright': '© {year} {brand}. All rights reserved.',
        'email.footer.automatic': 'This is an automatic message — please do not reply.',
    },
    'pt-br': {
        'error.notFound': 'Não encontrado.',
        'error.photoFileMissing': 'O arquivo {name} não está mais onde o catálogo esperava.',
        'error.restoreConflict': 'Não foi possível restaurar {name}: já existe um arquivo com esse nome em raw/.',

        'email.greeting': 'Olá, {name},',
        'email.footer.copyright': '© {year} {brand}. Todos os direitos reservados.',
        'email.footer.automatic': 'Esta é uma mensagem automática — não responda.',
    },
}


def normalize_language(value):
    """Normaliza um codigo de idioma para um dos suportados; senao, EN."""
    lang = (value or '').strip().lower().replace('_', '-')
    if lang in SUPPORTED_LANGUAGES:
        return lang
    # 'pt', 'pt-PT', 'pt-br-x' -> pt-br; qualquer outro -> EN
    if lang.split('-')[0] == 'pt':
        return 'pt-br'
    if lang.split('-')[0] == 'en':
        return 'en'
    return DEFAULT_LANGUAGE


def translate(key, language=None, default=None, **params):
    """Traduz ``key`` no idioma dado, com fallback obrigatorio para EN."""
    lang = normalize_language(language)
    text = CATALOGS.get(lang, {}).get(key)
    if text is None:
        text = CATALOGS[DEFAULT_LANGUAGE].get(key)
    if text is None:
        text = default if default is not None else key
    if params:
        try:
            return text.format(**params)
        except (KeyError, IndexError):
            return text
    return text


# Alias curto, no mesmo espirito do t() do frontend.
t = translate


#: Cookie de longa duracao com o ultimo idioma usado.
#:
#: E o que faz a escolha sobreviver entre visitas. Quem ESCREVE e o frontend
#: (ao trocar o idioma); aqui so se le, para responder no mesmo idioma.
#:
#: **Ao forkar:** renomeie aqui E em `frontend/src/lib/language.js`. O nome
#: esta repetido de proposito — divergir faz o cookie ser escrito com um nome
#: e procurado com outro, sem erro nenhum.
LANGUAGE_COOKIE_NAME = 'photoeditor_ln'


def language_for_request(request):
    """Idioma da requisicao: cookie -> Accept-Language -> EN.

    O cookie vem antes do navegador porque e uma escolha EXPLICITA de quem usa
    o sistema, e o navegador e a configuracao do aparelho — muita gente usa o
    sistema operacional em ingles e prefere ler em portugues.
    """
    cookie = (request.COOKIES.get(LANGUAGE_COOKIE_NAME) or '').strip()
    if cookie:
        normalizado = normalize_language(cookie)
        # So aceita o cookie quando ele de fato nomeia um idioma suportado: um
        # valor editado a mao nao pode travar a deteccao no primeiro degrau.
        if cookie.lower().replace('_', '-') in SUPPORTED_LANGUAGES:
            return normalizado

    header = (request.META.get('HTTP_ACCEPT_LANGUAGE') or '').split(',')[0]
    return normalize_language(header)


def tr(request, key, default=None, **params):
    """Traduz no idioma da requisicao — atalho para uso dentro das views."""
    return translate(key, language_for_request(request), default=default, **params)
