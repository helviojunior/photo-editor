pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Painel do merge (MergePanel.jsx): a rampa de transparência e as camadas —
// uma linha por foto de origem, de cima para baixo (a última camada fica por
// cima de todas), com a área, o alinhamento, a visibilidade e a opacidade; e
// a base embaixo, onde a área é o objeto DELA, que a opacidade esmaece. A
// linha ativa é a que aparece à esquerda (e recebe o pincel); uma por vez,
// então as linhas são um grupo de rádio.
ColumnLayout {
    id: panel

    /// Pincel na área da foto ``photoId`` (S).
    signal selectRequested(string photoId)
    signal removeRequested(string photoId, string fileName)

    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        SectionTitle {
            text: qsTr("Transparency")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8

            Repeater {
                model: [
                    { id: "fadeIn", text: qsTr("Fade in") },
                    { id: "fadeOut", text: qsTr("Fade out") },
                    { id: "solid", text: qsTr("Solid") }
                ]
                Button {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    size: "sm"
                    variant: "outline"
                    text: modelData.text
                    onClicked: App.merge.applyRamp(modelData.id)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        SectionTitle {
            text: qsTr("Layers")
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Paint the area to extract from each photo (S). Only the painted area goes into the result; the rest of the photo is used to align it to the base. On the base, paint its own object to fade it.")
            font.pixelSize: Theme.fontXs
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }

        Repeater {
            model: App.merge.layers.slice().reverse()

            MergeLayerRow {
                required property var modelData
                Layout.fillWidth: true
                entry: modelData
                onSelectRequested: panel.selectRequested(modelData.photoId)
                onRemoveRequested: panel.removeRequested(modelData.photoId, modelData.fileName)
            }
        }

        MergeLayerRow {
            Layout.fillWidth: true
            isBase: true
            entry: {
                const b = App.merge.base;
                const l = App.merge.baseLayer;
                return {
                    photoId: b.id, fileName: b.fileName, thumbUrl: b.thumbUrl, available: true,
                    mask: l.mask, hasMask: l.hasMask, opacity: l.opacity, visible: true,
                    active: l.active, align: null
                };
            }
            onSelectRequested: panel.selectRequested(App.merge.base.id)
        }
    }
}
