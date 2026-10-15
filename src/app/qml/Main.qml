pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC
import QtCore
import PhotoEditor

// Janela do app: menus, cabeçalho (AppLayout.jsx) e a tela de App.page.
//
//   "home"      pages/HomePage.qml
//   "editor"    pages/EditorPage.qml   (mantido carregado enquanto há projeto)
//   "merge"     pages/MergePage.qml
//   "instagram" pages/InstagramSettingsPage.qml
//
// Também é daqui: os diálogos globais (DialogHost do singleton Dialogs), o
// modal de erro de qualquer ``App.failed``, a tela de espera (App.loading), o
// Sobre e a confirmação de fechar com exportação em andamento.
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

    menuBar: MainMenuBar {
        flows: projectFlows
        window: window
        onAboutRequested: about.open()
    }

    header: AppHeader {
        onInstagramRequested: App.page = "instagram"
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
    Item {
        anchors.fill: parent

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

    // Por cima de tudo (inclusive dos modais): a espera do abrir/copiar.
    LoadingOverlay {
        parent: QQC.Overlay.overlay
        anchors.fill: parent
        z: 1000
    }

    DialogHost {}
}
