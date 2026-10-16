pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Camadas da foto (LayersSection do LayersPanel.jsx): o "restante" (os
// ajustes da própria foto) e uma linha por área extraída. A ativa é a que os
// sliders, os presets e o Auto editam; clicar nela escolhe. Uma escolha só
// por vez: as linhas são um grupo de rádio, não o SelectionCheck (regra 2.3).
ColumnLayout {
    id: section

    property bool disabled: false

    /// Selecionar área: '' = camada nova, ou o id da camada a editar.
    signal selectRequested(string layerId)

    readonly property var layers: App.editor.layers
    readonly property bool full: layers.length >= App.editor.maxLayers
    readonly property var rows: {
        const out = [{ id: "", label: layers.length ? qsTr("Rest of the photo") : qsTr("Whole photo") }];
        for (let i = 0; i < layers.length; ++i)
            out.push({ id: layers[i].id, label: qsTr("Selection %1").arg(i + 1) });
        return out;
    }

    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        SectionTitle {
            Layout.fillWidth: true
            text: qsTr("Layers")
        }

        Button {
            id: newButton
            variant: "ghost"
            size: "sm"
            iconName: "plus"
            text: qsTr("Select area")
            disabled: section.disabled || section.full
            onClicked: section.selectRequested("")

            Tip {
                text: section.full ? qsTr("Up to %1 layers per photo.").arg(App.editor.maxLayers)
                                   : qsTr("Select an area to extract as a layer") + Keymap.hint("selectArea")
                visible: newButton.hovered
            }
        }
    }

    Repeater {
        model: section.rows

        Rectangle {
            id: row

            required property var modelData
            readonly property bool active: modelData.id === App.editor.activeLayer

            Layout.fillWidth: true
            implicitHeight: 32
            radius: Theme.radiusMd
            color: active ? Theme.alpha(Theme.brand400, 0.15) : rowMouse.containsMouse ? Theme.muted : "transparent"
            border.width: 1
            border.color: active ? Theme.brand400 : Theme.border
            activeFocusOnTab: true

            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.label
            Accessible.checkable: true
            Accessible.checked: active
            Keys.onReturnPressed: App.editor.activeLayer = modelData.id
            Keys.onSpacePressed: App.editor.activeLayer = modelData.id

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: App.editor.activeLayer = row.modelData.id
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 8

                Icon {
                    name: "layers"
                    size: 14
                    color: row.active ? Theme.brand400 : (rowMouse.containsMouse ? Theme.foreground : Theme.mutedForeground)
                }
                Text {
                    Layout.fillWidth: true
                    text: row.modelData.label
                    font.pixelSize: Theme.fontSm
                    color: row.active || rowMouse.containsMouse ? Theme.foreground : Theme.mutedForeground
                    elide: Text.ElideRight
                }
                SmallIconButton {
                    visible: row.modelData.id !== ""
                    iconName: "pencil"
                    tooltip: qsTr("Edit area")
                    onClicked: section.selectRequested(row.modelData.id)
                }
                SmallIconButton {
                    visible: row.modelData.id !== ""
                    iconName: "trash-2"
                    tooltip: qsTr("Delete layer")
                    onClicked: App.editor.deleteLayer(row.modelData.id)
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: parent.radius + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: row.activeFocus
            }
        }
    }
}
