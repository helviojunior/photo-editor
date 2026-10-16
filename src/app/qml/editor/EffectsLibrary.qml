pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Aba Efeitos do painel: a biblioteca (App.editor.effectCatalog) em cartões,
// da camada ativa. Vários podem estar ligados ao mesmo tempo; cada cartão
// tem a miniatura, o nome e o Toggle (ligar/desligar é um estado da foto —
// regra 2.2; clicar no cartão inteiro também alterna). Os ligados ganham,
// embaixo, o slider da intensidade (1–100), na ordem em que o motor os
// aplica — a do catálogo, não a de quem ligou primeiro.
//
// Ligar/desligar grava na hora (toggleEffect); o slider muda o rascunho
// (setEffect) e grava ao soltar (commit).
ColumnLayout {
    id: library

    readonly property var catalog: App.editor.effectCatalog
    readonly property var active: App.editor.effects

    function amountOf(id) {
        const e = active.find((x) => x.id === id);
        return e ? e.amount : 0;
    }

    spacing: 12

    GridLayout {
        id: grid

        Layout.fillWidth: true
        columns: width >= 360 ? 3 : 2
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            model: library.catalog

            Rectangle {
                id: card

                required property var modelData
                readonly property bool on: library.amountOf(modelData.id) > 0

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: swatch.height + nameRow.implicitHeight + 14
                radius: Theme.radiusMd
                color: on ? Theme.brandSoft : cardMouse.containsMouse ? Theme.muted : "transparent"
                border.width: 1
                border.color: on ? Theme.brand400 : Theme.border
                clip: true

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.editor.toggleEffect(card.modelData.id)
                }

                EffectSwatch {
                    id: swatch
                    x: 4
                    y: 4
                    width: parent.width - 8
                    height: 52
                    effect: card.modelData.id
                    opacity: card.on || cardMouse.containsMouse ? 1 : 0.85
                }

                RowLayout {
                    id: nameRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: swatch.bottom
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    anchors.topMargin: 4
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: Messages.effectName(card.modelData.id)
                        font.pixelSize: Theme.fontSm
                        font.weight: card.on ? Font.DemiBold : Font.Normal
                        color: card.on ? Theme.foreground : Theme.mutedForeground
                        elide: Text.ElideRight
                    }
                    Toggle {
                        checked: card.on
                        Accessible.name: Messages.effectName(card.modelData.id)
                        onToggled: App.editor.toggleEffect(card.modelData.id)
                    }
                }
            }
        }
    }

    // Os ligados, na ordem em que o motor aplica.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        SectionTitle {
            text: qsTr("Applied effects")
        }
        Text {
            Layout.fillWidth: true
            text: library.active.length === 0
                ? qsTr("None yet. Turn on one or more effects above — they can be combined.")
                : qsTr("Applied in this order: %1").arg(library.active.map((e) => Messages.effectName(e.id)).join(" → "))
            font.pixelSize: Theme.fontXs
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }

        // Modelo = o catálogo (fixo), não a lista dos ligados: ela muda a cada
        // movimento do slider e recriaria o delegate no meio do arraste.
        Repeater {
            model: library.catalog

            ValueSlider {
                required property var modelData
                readonly property int amount: library.amountOf(modelData.id)
                Layout.fillWidth: true
                visible: amount > 0
                implicitHeight: 52
                label: Messages.effectName(modelData.id)
                from: 1
                to: 100
                stepSize: 1
                showSign: false
                defaultValue: modelData.defaultAmount
                value: amount
                onMoved: (v) => App.editor.setEffect(modelData.id, Math.round(v))
                onReleased: App.editor.commit()
            }
        }
    }
}
