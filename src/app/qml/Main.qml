pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC
import QtCore
import PhotoEditor

// Janela do app: cabeçalho (AppLayout.jsx), trilho lateral e a tela de App.page.
//
//   "home"      pages/HomePage.qml
//   "editor"    pages/EditorPage.qml   (mantido carregado enquanto há projeto)
//   "merge"     pages/MergePage.qml
//   "instagram" pages/InstagramSettingsPage.qml
//
// À esquerda de toda tela, o trilho lateral (ActivityRail); no cabeçalho, a
// busca de ações, desfazer/refazer e Exportar (AppHeader).
//
// Também é daqui: os diálogos globais (DialogHost do singleton Dialogs), o
// modal de erro de qualquer ``App.failed``, a tela de espera (App.loading), o
// Sobre, a paleta de comandos, o editor de atalhos, os atalhos que valem em
// qualquer tela (Keymap) e a confirmação de fechar com exportação em andamento.
QQC.ApplicationWindow {
    id: window

    // Tela anterior, para o "Voltar" do Instagram.
    property string previousPage: "home"
    property string _lastPage: App.page
    // Fechar confirmado com exportação rodando.
    property bool _closeConfirmed: false

    minimumWidth: 900
    minimumHeight: 600
    visible: false
    color: Theme.background
    title: App.hasProject ? qsTr("%1 — %2").arg(App.projectName).arg(App.brandName) : App.brandName

    // Geometria lembrada entre sessões; na primeira vez, 85% da tela, centrada.
    Settings {
        id: geometry
        category: "window"
        property int x: 0
        property int y: 0
        property int width: 0
        property int height: 0
        property bool maximized: false
    }

    Component.onCompleted: {
        if (geometry.width >= minimumWidth && geometry.height >= minimumHeight) {
            x = geometry.x;
            y = geometry.y;
            width = geometry.width;
            height = geometry.height;
        } else {
            width = Math.max(minimumWidth, Math.round(Screen.desktopAvailableWidth * 0.85));
            height = Math.max(minimumHeight, Math.round(Screen.desktopAvailableHeight * 0.85));
            x = Math.round((Screen.desktopAvailableWidth - width) / 2) + Screen.virtualX;
            y = Math.round((Screen.desktopAvailableHeight - height) / 2) + Screen.virtualY;
        }
        if (geometry.maximized)
            showMaximized();
        else
            show();
    }

    function saveGeometry() {
        geometry.maximized = visibility === Window.Maximized;
        if (visibility === Window.Windowed) {
            geometry.x = x;
            geometry.y = y;
            geometry.width = width;
            geometry.height = height;
        }
    }

    onClosing: (close) => {
        if (!_closeConfirmed && App.editor.exportStatus.running === true) {
            close.accepted = false;
            Dialogs.confirm({
                title: qsTr("Export in progress"),
                description: qsTr("An export is still running. Closing now stops it; the photos already written stay in publicar/. Close anyway?"),
                variant: "warning",
                confirmLabel: qsTr("Close"),
                confirmVariant: "destructive"
            }, () => {
                window._closeConfirmed = true;
                window.close();
            });
            return;
        }
        saveGeometry();
    }

    header: AppHeader {
        onSearchRequested: palette.open()
        onExportRequested: window.openExport()
    }

    // ------------------------------------------------------------------ //
    // Ações (trilho, cabeçalho, paleta e atalhos)
    // ------------------------------------------------------------------ //

    /// O EditorPage carregado (vive enquanto há projeto), ou null.
    readonly property var editorPage: editorLoader.item

    /// Volta ao editor; do merge, pelo mesmo caminho do "← Editor" (grava o
    /// que falta antes de sair).
    function showEditor() {
        if (!App.hasProject)
            return;
        if (App.page === "merge")
            App.merge.close();
        if (App.page !== "editor")
            App.page = "editor";
    }

    /// Ação do editor (EditorPage.run) vinda da paleta: abre o editor antes.
    function runEditor(id) {
        showEditor();
        Qt.callLater(() => {
            if (window.editorPage)
                window.editorPage.run(id);
        });
    }

    // Exportar e Publicar são do EditorPage (vivo enquanto há projeto); os
    // diálogos aparecem por cima de qualquer tela.
    function openExport() {
        if (editorPage)
            editorPage.openExport();
    }

    function openPublish() {
        if (editorPage)
            editorPage.openPublish();
    }

    function toggleFullScreen() {
        visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen;
    }

    /// Ações do Keymap que valem em qualquer tela.
    function runAction(id) {
        switch (id) {
        case "palette": palette.opened ? palette.close() : palette.open(); break;
        case "shortcuts": shortcutsDialog.open(); break;
        case "theme": App.darkTheme = !App.darkTheme; break;
        case "export": openExport(); break;
        case "publish": openPublish(); break;
        case "newProject": projectFlows.newProject(); break;
        case "openProject": projectFlows.openDialog(); break;
        case "closeProject": App.closeProject(); break;
        case "home": App.page = "home"; break;
        case "quit": window.close(); break;
        case "fullScreen": toggleFullScreen(); break;
        }
    }

    // Atalhos de qualquer tela (não há barra de menus: os de Arquivo e Exibir
    // de antes moram aqui, e os menus do trilho só mostram a tecla).
    readonly property bool globalKeys: !Keymap.recording && Dialogs.current === null
    readonly property var globalShortcuts: [
        { id: "palette", keys: Keymap.keys("palette"), enabled: true },
        { id: "shortcuts", keys: Keymap.keys("shortcuts"), enabled: true },
        { id: "theme", keys: Keymap.keys("theme"), enabled: true },
        { id: "export", keys: Keymap.keys("export"), enabled: App.hasProject },
        { id: "publish", keys: Keymap.keys("publish"),
          enabled: App.hasProject && App.editor.hasInstagramVersions && !App.editor.picking },
        { id: "newProject", keys: Keymap.keys("newProject"), enabled: true },
        { id: "openProject", keys: Keymap.keys("openProject"), enabled: true },
        { id: "closeProject", keys: Keymap.keys("closeProject"), enabled: App.hasProject },
        { id: "home", keys: Keymap.keys("home"), enabled: true },
        { id: "quit", keys: Keymap.keys("quit"), enabled: true },
        { id: "fullScreen", keys: Keymap.keys("fullScreen"), enabled: true }
    ]

    // O Shortcut acha a janela pelo Item pai: cada um vai num Item vazio.
    Repeater {
        model: window.globalShortcuts
        delegate: Item {
            id: binding
            required property var modelData
            Shortcut {
                sequences: binding.modelData.keys
                autoRepeat: false
                enabled: window.globalKeys && binding.modelData.enabled && binding.modelData.keys.length > 0
                onActivated: window.runAction(binding.modelData.id)
            }
        }
    }

    /// Comandos da paleta: os do editor só com projeto aberto; os do app sempre.
    readonly property var commands: {
        const out = [];
        // keywords: o id da ação em palavras ("cropMode" -> "crop mode").
        const add = (name, actionId, icon, run) => out.push({
            name: name, shortcut: actionId ? Keymap.label(actionId) : "", icon: icon, run: run,
            keywords: actionId ? actionId.replace(/([A-Z])/g, " $1").toLowerCase() : ""
        });
        if (App.hasProject) {
            const editorAction = (id, name, icon) => add(name || Keymap.action(id).name, id,
                                                         icon || Keymap.action(id).icon, () => window.runEditor(id));
            // Só nas fotos se edita; arquivadas e lixeira têm os comandos delas.
            const view = App.editor.view;
            const editable = view === "photos";
            editorAction("nextPhoto", qsTr("Next photo"));
            editorAction("previousPhoto", qsTr("Previous photo"));
            if (editable) {
                editorAction("auto", qsTr("Auto adjust"));
                editorAction("reset");
                editorAction("crop", qsTr("Crop mode"));
                editorAction("selectArea", qsTr("Select area"));
                editorAction("nextLayer");
                editorAction("instagram", App.editor.current.isInstagram === true
                    ? qsTr("Back to the photo") : qsTr("Instagram version"));
                editorAction("duplicate");
                editorAction("cover", App.editor.current.isCover === true ? qsTr("Unset cover") : qsTr("Set as cover"));
                editorAction("archivePhoto");
            } else {
                editorAction("restorePhoto");
            }
            if (view !== "trash")
                editorAction("deletePhoto");
            if (view === "trash") {
                editorAction("purgePhoto");
                if (App.editor.trashCount > 0)
                    editorAction("emptyTrash");
            }
            if (view !== "photos")
                editorAction("showPhotos");
            if (view !== "archived")
                editorAction("showArchived");
            if (view !== "trash")
                editorAction("showTrash");
            add(qsTr("Undo"), "undo", "undo-2", () => App.editor.undo());
            add(qsTr("Redo"), "redo", "redo-2", () => App.editor.redo());
            if (editable) {
                editorAction("rescan");
                editorAction("merge", App.editor.picking ? qsTr("Exit merge selection") : "");
            }
            add(qsTr("Export"), "export", "upload", () => window.openExport());
            add(Keymap.action("publish").name, "publish", "instagram", () => window.openPublish());
            for (const preset of (editable ? App.editor.presets : [])) {
                const id = preset.id;
                add(qsTr("Preset: %1").arg(Messages.presetName(id)), "", "palette", () => {
                    window.showEditor();
                    App.editor.togglePreset(id);
                });
            }
        }
        add(qsTr("Open project…"), "openProject", "folder-open", () => projectFlows.openDialog());
        add(qsTr("New project…"), "newProject", "folder-plus", () => projectFlows.newProject());
        if (App.hasProject) {
            add(qsTr("Close project"), "closeProject", "folder-x", () => App.closeProject());
            add(qsTr("Show project folder"), "", "folder-search", () => App.revealInFolder(App.projectPath));
        }
        add(qsTr("Projects"), "home", "folder", () => App.page = "home");
        add(qsTr("Instagram settings"), "", "instagram", () => App.page = "instagram");
        add(qsTr("About %1").arg(App.brandName), "", "info", () => about.open());
        add(qsTr("Keyboard shortcuts"), "shortcuts", "keyboard", () => shortcutsDialog.open());
        for (const language of App.languages) {
            const code = language.code;
            add(qsTr("Language: %1").arg(language.name), "", "languages", () => App.setLanguage(code));
        }
        add(Theme.dark ? qsTr("Light theme") : qsTr("Dark theme"), "theme", Theme.dark ? "sun" : "moon",
            () => App.darkTheme = !App.darkTheme);
        add(qsTr("Full screen"), "fullScreen", "maximize", () => window.toggleFullScreen());
        add(qsTr("Open logs folder"), "", "file-text", () => App.openLogsFolder());
        return out;
    }

    Connections {
        target: App

        function onPageChanged() {
            if (window._lastPage !== App.page)
                window.previousPage = window._lastPage;
            window._lastPage = App.page;
        }

        // Todo erro de ação do C++ (App, editor, merge, Instagram) vira modal.
        function onFailed(key, params) {
            Dialogs.alert(Messages.errorDialog(key, params));
        }
    }

    // ------------------------------------------------------------------ //
    // Telas
    // ------------------------------------------------------------------ //
    ActivityRail {
        id: rail
        objectName: "rail"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        flows: projectFlows
        onEditorRequested: window.showEditor()
        onAboutRequested: about.open()
        onShortcutsRequested: shortcutsDialog.open()
        onFullScreenRequested: window.toggleFullScreen()
        onQuitRequested: window.close()
        onExportRequested: window.openExport()
        onPublishRequested: window.openPublish()
    }

    Item {
        anchors.left: rail.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Loader {
            anchors.fill: parent
            active: App.page === "home"
            visible: active
            sourceComponent: HomePage {
                flows: projectFlows
            }
        }

        // O editor fica vivo enquanto o projeto está aberto: ir ao Instagram
        // ou à Home e voltar não perde zoom, rolagem nem painéis.
        Loader {
            id: editorLoader
            anchors.fill: parent
            active: App.hasProject
            visible: App.page === "editor"
            sourceComponent: EditorPage {}
        }

        Loader {
            anchors.fill: parent
            active: App.page === "merge"
            visible: active
            sourceComponent: MergePage {}
        }

        Loader {
            anchors.fill: parent
            active: App.page === "instagram"
            visible: active
            sourceComponent: InstagramSettingsPage {
                onBackRequested: {
                    let back = window.previousPage;
                    if ((back === "editor" || back === "merge") && !App.hasProject)
                        back = "home";
                    if (back === "merge" && !App.merge.open)
                        back = "editor";
                    App.page = back === "instagram" ? "home" : back;
                }
            }
        }
    }

    ProjectFlows {
        id: projectFlows
    }

    AboutDialog {
        id: about
    }

    CommandPalette {
        id: palette
        objectName: "commandPalette"
        commands: window.commands
    }

    ShortcutsDialog {
        id: shortcutsDialog
        objectName: "shortcutsDialog"
    }

    // Por cima de tudo (inclusive dos modais): a espera do abrir/copiar.
    LoadingOverlay {
        parent: QQC.Overlay.overlay
        anchors.fill: parent
        z: 1000
    }

    DialogHost {}
}
