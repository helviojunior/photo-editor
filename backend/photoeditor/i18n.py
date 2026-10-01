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
        'layers.maskNotFound': 'The selection no longer exists. Start a new one.',
        'layers.invalidStroke': 'The brush stroke is empty or invalid.',
        'merge.tooFew': 'Select at least two photos: the first is the base, the others become layers.',
        'merge.alreadyMerged': 'Already part of a merge: {name}. Undo that merge first.',
        'merge.notALayer': 'This photo is not a layer of the merge.',
        'merge.instagramVersion': 'An Instagram version cannot be part of a merge: it already starts from the merge of its photo.',
        'instagram.notForVersion': 'Not available for the Instagram version: use the original photo.',
        'instagram.versionExists': 'This photo already has another Instagram version.',
        'instagram.notConnected': 'No Instagram account is connected. Connect one in Settings > Instagram.',
        'instagram.missingCredentials': 'Enter the Instagram username and password.',
        'instagram.missingCode': 'Enter the code.',
        'instagram.noCodePending': 'There is no login waiting for a code. Start again.',
        'instagram.badCredentials': 'Wrong username or password.',
        'instagram.badCode': 'The code was not accepted. Try again with a new code.',
        'instagram.challenge': 'Instagram asked to confirm this login. Open the Instagram app, confirm it was you and try again.',
        'instagram.sessionExpired': 'The Instagram session has expired. Connect the account again in Settings > Instagram.',
        'instagram.wait': 'Instagram is limiting requests from this account. Wait a few minutes and try again.',
        'instagram.suspended': 'Instagram reports that this account is suspended.',
        'instagram.network': 'Could not reach Instagram. Check the internet connection.',
        'instagram.failed': 'Instagram did not accept the request.',
        'instagram.publishFailed': 'The post could not be published.',
        'instagram.publishRunning': 'A post is already being published.',
        'instagram.noPhotos': 'Choose at least one Instagram version to publish.',
        'instagram.tooMany': 'An Instagram post takes at most {max} photos.',
        'instagram.captionTooLong': 'The caption is longer than the {max} characters Instagram accepts.',

        # E-mails
        'email.greeting': 'Hello, {name},',
        'email.footer.copyright': '© {year} {brand}. All rights reserved.',
        'email.footer.automatic': 'This is an automatic message — please do not reply.',
    },
    'pt-br': {
        'error.notFound': 'Não encontrado.',
        'error.photoFileMissing': 'O arquivo {name} não está mais onde o catálogo esperava.',
        'error.restoreConflict': 'Não foi possível restaurar {name}: já existe um arquivo com esse nome em raw/.',
        'layers.maskNotFound': 'A seleção não existe mais. Comece uma nova.',
        'layers.invalidStroke': 'O traço do pincel está vazio ou é inválido.',
        'merge.tooFew': 'Selecione pelo menos duas fotos: a primeira é a base, as outras viram camadas.',
        'merge.alreadyMerged': 'Já faz parte de um merge: {name}. Desfaça aquele merge antes.',
        'merge.notALayer': 'Esta foto não é uma camada do merge.',
        'merge.instagramVersion': 'Uma versão Instagram não pode entrar num merge: ela já parte do merge da foto dela.',
        'instagram.notForVersion': 'Não disponível na versão Instagram: use a foto original.',
        'instagram.versionExists': 'Esta foto já tem outra versão Instagram.',
        'instagram.notConnected': 'Nenhuma conta do Instagram conectada. Conecte uma em Configurações > Instagram.',
        'instagram.missingCredentials': 'Informe o usuário e a senha do Instagram.',
        'instagram.missingCode': 'Informe o código.',
        'instagram.noCodePending': 'Não há login esperando código. Comece de novo.',
        'instagram.badCredentials': 'Usuário ou senha incorretos.',
        'instagram.badCode': 'O código não foi aceito. Tente de novo com um código novo.',
        'instagram.challenge': 'O Instagram pediu para confirmar este acesso. Abra o app do Instagram, confirme que foi você e tente de novo.',
        'instagram.sessionExpired': 'A sessão do Instagram expirou. Conecte a conta de novo em Configurações > Instagram.',
        'instagram.wait': 'O Instagram está limitando as requisições desta conta. Aguarde alguns minutos e tente de novo.',
        'instagram.suspended': 'O Instagram informa que esta conta está suspensa.',
        'instagram.network': 'Não foi possível falar com o Instagram. Confira a conexão com a internet.',
        'instagram.failed': 'O Instagram não aceitou a requisição.',
        'instagram.publishFailed': 'Não foi possível publicar o post.',
        'instagram.publishRunning': 'Já há um post sendo publicado.',
        'instagram.noPhotos': 'Escolha pelo menos uma versão Instagram para publicar.',
        'instagram.tooMany': 'Um post do Instagram aceita no máximo {max} fotos.',
        'instagram.captionTooLong': 'A legenda passa dos {max} caracteres que o Instagram aceita.',

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
