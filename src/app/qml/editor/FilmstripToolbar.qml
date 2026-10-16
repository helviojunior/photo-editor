import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Barra da filmstrip (Editor.jsx): posição, mensagem da última ação, ordem
// das fotos e os mesmos comandos dos atalhos, para quem não tem teclado.
// Publicar abre o diálogo (sinal para o EditorPage); Exportar, desfazer e
// refazer ficam no cabeçalho (AppHeader).
Rectangle {
    id: bar

    signal publishRequested()

    readonly property var current: App.editor.current
    readonly property bool hasCurrent: current.id !== undefined && current.id !== ""
    readonly property bool picking: App.editor.picking
    readonly property bool isInstagram: current.isInstagram === true
    readonly property var sortOptions: [
        { value: "date_asc", text: qsTr("Date/time ↑ (oldest first)") },
        { value: "date_desc", text: qsTr("Date/time ↓ (newest first)") },
        { value: "name_asc", text: qsTr("Name A–Z") },
        { value: "name_desc", text: qsTr("Name Z–A") }
    ]

    implicitHeight: Math.max(44, row.implicitHeight + 8)
    color: Theme.card

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.bottomMargin: 1
        spacing: 16

        Text {
            text: bar.hasCurrent
                ? qsTr("Photo %1 of %2").arg(bar.current.position).arg(bar.current.total)
                : qsTr("Loading…")
            font.pixelSize: Theme.fontSm
            font.weight: Font.Medium
            color: Theme.foreground
        }

        Select {
            id: sort
            implicitHeight: 32
            font.pixelSize: Theme.fontSm
            model: bar.sortOptions
            textRole: "text"
            valueRole: "value"
            currentIndex: bar.sortOptions.findIndex((o) => o.value === App.editor.sortMode)
            Accessible.name: qsTr("Sort photos")
            onActivated: App.editor.sortMode = currentValue
        }

        Text {
            Layout.fillWidth: true
            text: bar.picking
                ? qsTr("%1 selected for the merge — the first is the base").arg(App.editor.picked.length)
                : Messages.status(App.editor.statusMessage, App.editor.statusParams)
            font.pixelSize: Theme.fontSm
            color: Theme.mutedForeground
            elide: Text.ElideRight
            Accessible.role: Accessible.StatusBar
        }

        RowLayout {
            spacing: 4

            // Merge: marcar fotos na faixa
            Button {
                visible: bar.picking
                size: "sm"
                iconName: "combine"
                text: qsTr("Create merge")
                loading: App.editor.busy
                disabled: App.editor.picked.length < 2
                onClicked: App.editor.createMerge()
            }
            Button {
                visible: bar.picking
                size: "sm"
                variant: "ghost"
                iconName: "x"
                text: qsTr("Exit selection")
                onClicked: App.editor.picking = false
            }
            Button {
                id: mergeButton
                visible: !bar.picking
                size: "sm"
                variant: "ghost"
                iconName: "combine"
                text: qsTr("Merge")
                disabled: App.editor.photos.count < 2
                onClicked: App.editor.picking = true

                Tip {
                    text: qsTr("Select photos to merge: the first is the base, the others become layers")
                    visible: mergeButton.hovered
                }
            }

            ToolButton {
                iconName: "chevron-left"
                tooltip: qsTr("Previous photo") + Keymap.hint("previousPhoto")
                enabled: bar.hasCurrent && bar.current.position > 1
                onClicked: App.editor.previous()
            }
            ToolButton {
                iconName: "chevron-right"
                tooltip: qsTr("Next photo") + Keymap.hint("nextPhoto")
                enabled: bar.hasCurrent && bar.current.position < bar.current.total
                onClicked: App.editor.next()
            }
            ToolButton {
                iconName: "instagram"
                tooltip: (bar.isInstagram ? qsTr("Back to the photo")
                       : bar.current.hasInstagram ? qsTr("Open the Instagram version")
                       : qsTr("Edit for Instagram")) + Keymap.hint("instagram")
                active: bar.isInstagram || bar.current.hasInstagram === true
                enabled: bar.hasCurrent && !bar.picking
                onClicked: App.editor.toggleInstagram()
            }
            ToolButton {
                iconName: "copy"
                tooltip: qsTr("Duplicate photo (virtual copy)") + Keymap.hint("duplicate")
                enabled: bar.hasCurrent && !bar.picking && !bar.isInstagram
                onClicked: App.editor.duplicateCurrent()
            }
            ToolButton {
                iconName: "star"
                tooltip: bar.current.isCover ? qsTr("Cover photo — click to unset")
                                             : qsTr("Set as cover (exported as capa.jpg)")
                active: bar.current.isCover === true
                filled: bar.current.isCover === true
                enabled: bar.hasCurrent && !bar.picking && !bar.isInstagram
                onClicked: App.editor.toggleCover()
            }
            ToolButton {
                iconName: "trash-2"
                tooltip: qsTr("Delete photo") + Keymap.hint("deletePhoto")
                enabled: bar.hasCurrent
                onClicked: App.editor.deleteCurrent()
            }
            Button {
                size: "sm"
                variant: "ghost"
                iconName: "refresh-cw"
                text: qsTr("Rescan")
                onClicked: App.editor.rescan()
            }
            Button {
                id: publishButton
                size: "sm"
                variant: "outline"
                iconName: "instagram"
                text: qsTr("Publish")
                disabled: !App.editor.hasInstagramVersions || bar.picking
                onClicked: bar.publishRequested()

                // O botão desabilitado não recebe hover: a dica fica num HoverHandler.
                HoverHandler { id: publishHover }
                Tip {
                    text: App.editor.hasInstagramVersions
                        ? qsTr("Publish on Instagram")
                        : qsTr("No photo has an Instagram version. Press I on a photo to create one.")
                    visible: publishHover.hovered
                }
            }
        }
    }
}
