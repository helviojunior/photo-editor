pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC
import PhotoEditor

// Menus da janela (os do desktop/window.py antigo): Arquivo, Exibir, Ajuda.
//
// Atalhos em texto ("Ctrl+O"; no macOS o Ctrl vira ⌘): um StandardKey pode
// valer várias teclas e o AppMenuItem só consegue mostrar uma.
//
// Uma MenuBar do Qt Quick Controls só, para os três SOs: no macOS o Qt 6.8 a
// transforma na barra de menus NATIVA do sistema (no topo da tela; dentro da
// janela ela não ocupa espaço) e os atalhos usam ⌘; no Windows/Linux ela é
// desenhada dentro da janela, com o tema (delegate/background abaixo). Assim
// não dependemos do Qt.labs.platform (que no Windows/Linux exige QtWidgets).
QQC.MenuBar {
    id: bar

    /// Fluxos de abrir/criar projeto (ProjectFlows do Main).
    required property var flows
    /// A janela, para tela cheia e sair.
    required property var window

    signal aboutRequested()

    delegate: QQC.MenuBarItem {
        id: barItem
        implicitHeight: 30
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: Theme.fontBase

        contentItem: Text {
            text: barItem.text.replace("&", "")
            font: barItem.font
            color: barItem.highlighted ? Theme.foreground : Theme.mutedForeground
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: barItem.highlighted ? Theme.accent : "transparent"
        }
    }

    background: Rectangle {
        implicitHeight: 30
        color: Theme.card
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    AppMenu {
        title: qsTr("&File")

        QQC.Action {
            text: qsTr("Home")
            shortcut: "Ctrl+Shift+H"
            onTriggered: App.page = "home"
        }
        QQC.Action {
            text: qsTr("New Project…")
            shortcut: "Ctrl+N"
            onTriggered: bar.flows.newProject()
        }
        QQC.Action {
            text: qsTr("Open Project Folder…")
            shortcut: "Ctrl+O"
            onTriggered: bar.flows.openDialog()
        }

        AppMenu {
            id: recentMenu
            title: qsTr("Open Recent")

            // Até 15 recentes; sem nenhum, um item apagado que avisa.
            Instantiator {
                model: App.recentProjects.length > 0 ? App.recentProjects.slice(0, 15) : [null]
                delegate: AppMenuItem {
                    id: recentItem
                    required property var modelData
                    text: recentItem.modelData ? recentItem.modelData.name : qsTr("No recent projects")
                    enabled: recentItem.modelData !== null
                    onTriggered: bar.flows.openRecent(recentItem.modelData)
                }
                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }
        }

        QQC.MenuSeparator {}

        QQC.Action {
            text: qsTr("Show Project Folder")
            enabled: App.hasProject
            onTriggered: App.revealInFolder(App.projectPath)
        }
        QQC.Action {
            text: qsTr("Close Project")
            shortcut: "Ctrl+W"
            enabled: App.hasProject
            onTriggered: App.closeProject()
        }

        QQC.MenuSeparator {}

        QQC.Action {
            text: qsTr("Quit")
            shortcut: "Ctrl+Q"
            onTriggered: bar.window.close()
        }
    }

    AppMenu {
        title: qsTr("&View")

        QQC.Action {
            text: qsTr("Toggle Full Screen")
            shortcut: Qt.platform.os === "osx" ? "Ctrl+Meta+F" : "F11"
            onTriggered: bar.window.visibility = bar.window.visibility === Window.FullScreen
                         ? Window.Windowed : Window.FullScreen
        }
    }

    AppMenu {
        title: qsTr("&Help")

        QQC.Action {
            text: qsTr("Open Logs Folder")
            onTriggered: App.openLogsFolder()
        }
        QQC.Action {
            text: qsTr("About %1").arg(App.brandName)
            onTriggered: bar.aboutRequested()
        }
    }
}
