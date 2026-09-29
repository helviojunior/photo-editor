"""Textos do shell desktop (menus, dialogos nativos, telas de espera).

Mesmas regras do resto do sistema (regra 4): EN e o padrao e o fallback, e
toda chave em PT-BR existe em EN. O idioma acompanha o que a pessoa escolheu
no app: o shell observa o cookie ``photoeditor_ln`` no navegador embarcado
(ver ``window.py``) — o mesmo nome de ``frontend/src/lib/language.js``.
"""
from PySide6.QtCore import QLocale

DEFAULT_LANGUAGE = 'en'
SUPPORTED_LANGUAGES = ('en', 'pt-br')
LANGUAGE_COOKIE = 'photoeditor_ln'

CATALOGS = {
    'en': {
        'menu.file': '&File',
        'menu.home': 'Home',
        'menu.open': 'Open Project Folder…',
        'menu.new': 'New Project…',
        'menu.recent': 'Open Recent',
        'menu.recent.empty': 'No recent projects',
        'menu.reveal': 'Show Project Folder',
        'menu.close': 'Close Project',
        'menu.quit': 'Quit',
        'menu.view': '&View',
        'menu.reload': 'Reload',
        'menu.zoomIn': 'Zoom In',
        'menu.zoomOut': 'Zoom Out',
        'menu.zoomReset': 'Actual Size',
        'menu.fullscreen': 'Toggle Full Screen',
        'menu.devtools': 'Developer Tools',
        'menu.help': '&Help',
        'menu.logs': 'Open Logs Folder',
        'menu.about': 'About {brand}',

        'dialog.open.title': 'Open project folder',
        'dialog.new.title': 'Choose or create the folder for the new project',
        'dialog.import.title': 'Choose the JPEG photos to add to the project',
        'dialog.import.filter': 'JPEG photos (*.jpg *.jpeg *.JPG *.JPEG)',
        'dialog.import.progress': 'Copying photos…',
        'dialog.import.cancel': 'Cancel',
        'dialog.save.title': 'Save file',

        'project.noRaw.title': 'No raw/ folder',
        'project.noRaw.text': 'The folder “{name}” has no raw/ subfolder, which is where the original JPEGs of the event go.',
        'project.noRaw.create': 'Create raw/ and continue',
        'project.noRaw.move': 'Move the {count} photos into raw/ and continue',
        'project.moveFailed': 'Some photos stayed where they were:',
        'project.noRaw.other': 'Choose another folder',
        'project.missing.title': 'Folder not found',
        'project.missing.text': 'The folder “{path}” no longer exists or cannot be read.',
        'project.locked.title': 'Project already open',
        'project.locked.text': 'The project “{name}” is already open in another {brand} window.',
        'project.nonEmpty.title': 'Folder is not empty',
        'project.nonEmpty.text': 'The folder “{name}” already has files. Use it as the new project anyway?',
        'project.importFailed': 'Some photos could not be copied:',

        'quit.exporting.title': 'Export in progress',
        'quit.exporting.text': 'An export is still running. Closing now stops it; the photos already written stay in publicar/. Close anyway?',
        'quit.exporting.confirm': 'Close',
        'common.cancel': 'Cancel',

        'loading.home': 'Starting…',
        'loading.project': 'Opening “{name}”…',
        'loading.hint': 'Reading the catalog. Large projects take a few seconds.',
        'error.title': 'The editor could not start',
        'error.text': 'The local server stopped unexpectedly. Details are in the log file:',
        'error.home': 'Back to Home',
        'error.retry': 'Try again',
        'error.logs': 'Open logs folder',

        'about.text': '{brand} {version}\n\nEvent photo editor.\nData: {data}\nLogs: {logs}',
    },
    'pt-br': {
        'menu.file': '&Arquivo',
        'menu.home': 'Início',
        'menu.open': 'Abrir Pasta de Projeto…',
        'menu.new': 'Novo Projeto…',
        'menu.recent': 'Abrir Recente',
        'menu.recent.empty': 'Nenhum projeto recente',
        'menu.reveal': 'Mostrar Pasta do Projeto',
        'menu.close': 'Fechar Projeto',
        'menu.quit': 'Sair',
        'menu.view': '&Exibir',
        'menu.reload': 'Recarregar',
        'menu.zoomIn': 'Aumentar Zoom',
        'menu.zoomOut': 'Diminuir Zoom',
        'menu.zoomReset': 'Tamanho Real',
        'menu.fullscreen': 'Tela Cheia',
        'menu.devtools': 'Ferramentas do Desenvolvedor',
        'menu.help': 'A&juda',
        'menu.logs': 'Abrir Pasta de Logs',
        'menu.about': 'Sobre o {brand}',

        'dialog.open.title': 'Abrir pasta de projeto',
        'dialog.new.title': 'Escolha ou crie a pasta do novo projeto',
        'dialog.import.title': 'Escolha as fotos JPEG para adicionar ao projeto',
        'dialog.import.filter': 'Fotos JPEG (*.jpg *.jpeg *.JPG *.JPEG)',
        'dialog.import.progress': 'Copiando fotos…',
        'dialog.import.cancel': 'Cancelar',
        'dialog.save.title': 'Salvar arquivo',

        'project.noRaw.title': 'Sem pasta raw/',
        'project.noRaw.text': 'A pasta “{name}” não tem a subpasta raw/, onde ficam os JPEGs originais do evento.',
        'project.noRaw.create': 'Criar raw/ e continuar',
        'project.noRaw.move': 'Mover as {count} fotos para raw/ e continuar',
        'project.moveFailed': 'Algumas fotos ficaram onde estavam:',
        'project.noRaw.other': 'Escolher outra pasta',
        'project.missing.title': 'Pasta não encontrada',
        'project.missing.text': 'A pasta “{path}” não existe mais ou não pode ser lida.',
        'project.locked.title': 'Projeto já aberto',
        'project.locked.text': 'O projeto “{name}” já está aberto em outra janela do {brand}.',
        'project.nonEmpty.title': 'Pasta não está vazia',
        'project.nonEmpty.text': 'A pasta “{name}” já tem arquivos. Usar mesmo assim como novo projeto?',
        'project.importFailed': 'Algumas fotos não puderam ser copiadas:',

        'quit.exporting.title': 'Exportação em andamento',
        'quit.exporting.text': 'Uma exportação ainda está rodando. Fechar agora a interrompe; as fotos já gravadas ficam em publicar/. Fechar mesmo assim?',
        'quit.exporting.confirm': 'Fechar',
        'common.cancel': 'Cancelar',

        'loading.home': 'Iniciando…',
        'loading.project': 'Abrindo “{name}”…',
        'loading.hint': 'Lendo o catálogo. Projetos grandes levam alguns segundos.',
        'error.title': 'O editor não conseguiu iniciar',
        'error.text': 'O servidor local parou inesperadamente. Os detalhes estão no arquivo de log:',
        'error.home': 'Voltar ao Início',
        'error.retry': 'Tentar de novo',
        'error.logs': 'Abrir pasta de logs',

        'about.text': '{brand} {version}\n\nEditor de fotos de eventos.\nDados: {data}\nLogs: {logs}',
    },
}


def normalize(value):
    lang = (value or '').strip().lower().replace('_', '-')
    if lang in SUPPORTED_LANGUAGES:
        return lang
    base = lang.split('-')[0]
    if base == 'pt':
        return 'pt-br'
    if base == 'en':
        return 'en'
    return None


def system_language():
    """Idioma do SO, se o sistema o fala; senao o padrao (EN)."""
    for name in QLocale.system().uiLanguages():
        lang = normalize(name)
        if lang:
            return lang
    return DEFAULT_LANGUAGE


class Translator:
    def __init__(self, language=None):
        self.language = normalize(language) or DEFAULT_LANGUAGE

    def __call__(self, key, **params):
        text = CATALOGS.get(self.language, {}).get(key)
        if text is None:
            text = CATALOGS[DEFAULT_LANGUAGE].get(key, key)
        if params:
            try:
                return text.format(**params)
            except (KeyError, IndexError):
                return text
        return text
