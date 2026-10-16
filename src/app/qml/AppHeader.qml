import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import PhotoEditor

// Cabeçalho do app (o TopBar do clivcut): à esquerda logo + versão e o nome
// do projeto aberto (clicar volta ao editor); ao centro a busca de ações
// (abre a paleta de comandos); à direita desfazer, refazer e Exportar.
// Projetos, idioma, Instagram e tema ficam no trilho lateral (ActivityRail).
// Baixo de propósito: cada pixel que ele não usa vai para as fotos.
Rectangle {
    id: header

    signal searchRequested()
    signal exportRequested()

    readonly property var exportStatus: App.editor.exportStatus || ({})
    readonly property bool exporting: exportStatus.running === true
    readonly property int exportPercent: exportStatus.total
        ? Math.round((exportStatus.done || 0) / exportStatus.total * 100) : 0

    implicitHeight: Theme.headerHeight + 1
    color: Theme.card

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    // Esquerda: logo e projeto.
    RowLayout {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -0.5
        spacing: 8

        Image {
            id: logo
            Layout.preferredHeight: 24
            Layout.preferredWidth: 24 * 2594 / 575
            source: Theme.dark ? "qrc:/qt/qml/PhotoEditor/assets/logo/photoe-dark.png"
                               : "qrc:/qt/qml/PhotoEditor/assets/logo/photoe-light.png"
            sourceSize.height: 24 * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        // A versão fica só no "Sobre" (trilho lateral): o cabeçalho é das ações.

        // Projeto aberto: o nome (clicar nele volta ao editor).
        Separator {
            vertical: true
            visible: App.hasProject
            Layout.preferredHeight: 20
            Layout.leftMargin: 4
            Layout.rightMargin: 4
        }
        Text {
            id: projectName
            visible: App.hasProject
            // Até a metade da janela menos meia busca (180) e folga: a busca,
            // centrada, sempre cabe. ``x`` já conta o logo e o separador.
            Layout.maximumWidth: Math.max(60, header.width / 2 - 196 - left.x - x)
            text: App.projectName
            font.pixelSize: Theme.fontBase
            font.weight: Font.Medium
            color: Theme.foreground
            elide: Text.ElideRight

            MouseArea {
                id: nameMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: App.page !== "editor" ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (App.page !== "editor" && App.page !== "merge")
                        App.page = "editor";
                }
            }
            Tip {
                text: App.projectPath
                visible: nameMouse.containsMouse
            }
        }
    }

    // Busca de ações (paleta de comandos), centrada na janela; encolhe para
    // não encostar no que está à esquerda e à direita, e some se não couber.
    T.AbstractButton {
        id: search

        readonly property real room: header.width - 2 * Math.max(left.width + left.x, right.width + 16) - 32

        anchors.centerIn: parent
        anchors.verticalCenterOffset: -0.5
        width: Math.min(360, room)
        height: 28
        visible: room >= 160
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: qsTr("Search actions…")
        onClicked: header.searchRequested()

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.background
            border.color: search.hovered || search.visualFocus ? Theme.brand400 : Theme.border
            Behavior on border.color { ColorAnimation { duration: Theme.animationFast } }
        }
        contentItem: RowLayout {
            spacing: 8
            Icon {
                Layout.leftMargin: 10
                name: "search"
                size: 14
                color: Theme.mutedForeground
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Search actions…")
                font.pixelSize: Theme.fontSm
                color: Theme.mutedForeground
                elide: Text.ElideRight
            }
            Keycap {
                Layout.rightMargin: 5
                text: Keymap.label("palette")
            }
        }
    }

    // Direita: desfazer, refazer e Exportar (só com projeto aberto).
    RowLayout {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -0.5
        visible: App.hasProject
        spacing: 2

        // Globais: a última ação de qualquer foto.
        IconButton {
            diameter: 32
            iconSize: 17
            iconName: "undo-2"
            enabled: App.editor.canUndo
            tooltip: App.editor.canUndo ? qsTr("Undo: %1").arg(Messages.actionName(App.editor.undoKind)) : qsTr("Undo")
            shortcut: Keymap.label("undo")
            onClicked: App.editor.undo()
        }
        IconButton {
            diameter: 32
            iconSize: 17
            iconName: "redo-2"
            enabled: App.editor.canRedo
            tooltip: App.editor.canRedo ? qsTr("Redo: %1").arg(Messages.actionName(App.editor.redoKind)) : qsTr("Redo")
            shortcut: Keymap.label("redo")
            onClicked: App.editor.redo()
        }

        // Exportando, o botão mostra o andamento e reabre o diálogo.
        Button {
            id: exportButton
            Layout.leftMargin: 8
            implicitHeight: 30
            size: "sm"
            iconName: "upload"
            loading: header.exporting
            enabled: true
            text: header.exporting ? qsTr("Exporting %1%").arg(header.exportPercent) : qsTr("Export")
            onClicked: header.exportRequested()

            Tip {
                text: Keymap.label("export") !== ""
                    ? qsTr("Export the active photos to publicar/") + "   " + Keymap.label("export")
                    : qsTr("Export the active photos to publicar/")
                visible: exportButton.hovered
                x: exportButton.width - implicitWidth
            }
        }
    }
}
