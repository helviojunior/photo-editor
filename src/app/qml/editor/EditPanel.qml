pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Painel de edição (EditPanel.jsx): Auto, Reset, crop, camadas, presets e os
// sliders do motor.
//
// Na versão Instagram o crop ganha a escolha da proporção — 4:5, 1:1 ou
// 1,91:1 — e o giro fica no limite dela: o quadro nunca sai do que o feed aceita.
//
// Sliders e presets editam a camada ATIVA (App.editor.activeLayer); sem
// camada ativa, os ajustes da própria foto. Arrastar muda o rascunho
// (setValue — a "Editada" acompanha); soltar grava (commit) — uma entrada no
// histórico por gesto, não uma por pixel arrastado.
//
// O crop é do EditorPage (ele guarda o baseScale): o painel pede por sinais.
ColumnLayout {
    id: panel

    property bool cropMode: false
    property bool disabled: false

    signal cropToggled()
    /// Mudança do quadro: mode "rotate" | "ratio" | "reset".
    signal cropEdited(var crop, string mode)
    signal cropCommitted()
    /// O quadro mudou de tamanho por fora (proporção, desfazer recorte).
    signal cropRestarted()
    signal selectRequested(string layerId)

    readonly property var current: App.editor.current
    readonly property bool isInstagram: current.isInstagram === true
    readonly property var crop: App.editor.crop
    readonly property var values: App.editor.values

    function sliderLabel(name) {
        switch (name) {
        case "exposure": return qsTr("Exposure");
        case "contrast": return qsTr("Contrast");
        case "highlights": return qsTr("Highlights");
        case "shadows": return qsTr("Shadows");
        case "whites": return qsTr("Whites");
        case "blacks": return qsTr("Blacks");
        case "temperature": return qsTr("Temperature");
        case "tint": return qsTr("Tint");
        case "vibrance": return qsTr("Vibrance");
        case "saturation": return qsTr("Saturation");
        }
        return name;
    }

    function presetLabel(id) {
        switch (id) {
        case "vivid": return qsTr("Vivid");
        case "soft": return qsTr("Soft");
        case "warm": return qsTr("Warm");
        case "cool": return qsTr("Cool");
        case "bw": return qsTr("B&W");
        case "matte": return qsTr("Matte");
        case "vintage": return qsTr("Vintage");
        }
        return id;
    }

    function ratioLabel(id) {
        switch (id) {
        case "4:5": return qsTr("4:5 — portrait (1080×1350)");
        case "1:1": return qsTr("1:1 — square (1080×1080)");
        case "1.91:1": return qsTr("1.91:1 — landscape (1080×566)");
        }
        return id;
    }

    function groupLabel(group) {
        return group === "light" ? qsTr("Light") : group === "color" ? qsTr("Color") : group;
    }

    spacing: 16
    enabled: !disabled
    opacity: disabled ? 0.6 : 1

    // Auto | Reset
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 8

        Button {
            id: autoButton
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            variant: "outline"
            size: "sm"
            iconName: "wand-sparkles"
            text: qsTr("Auto")
            onClicked: App.editor.autoAdjust()

            Tip {
                text: qsTr("Auto (A)")
                visible: autoButton.hovered
            }
        }
        Button {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            variant: "outline"
            size: "sm"
            iconName: "rotate-ccw"
            text: qsTr("Reset")
            onClicked: App.editor.reset()
        }
    }

    // Crop
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        Button {
            id: cropButton
            Layout.fillWidth: true
            variant: panel.cropMode ? "default" : "outline"
            size: "sm"
            iconName: panel.cropMode ? "check" : "crop"
            text: panel.cropMode ? qsTr("Done") : qsTr("Crop")
            Accessible.checkable: true
            Accessible.checked: panel.cropMode
            onClicked: panel.cropToggled()

            Tip {
                text: qsTr("Crop mode (C)")
                visible: cropButton.hovered
            }
        }

        // Versão Instagram: a proporção do feed, com o desenho de cada uma.
        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.isInstagram
            spacing: 4

            Text {
                text: qsTr("Instagram format")
                font.pixelSize: Theme.fontSm
                color: Theme.foreground
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 6
                Accessible.name: qsTr("Instagram format")

                Repeater {
                    model: App.editor.instagramRatios

                    PillButton {
                        id: ratioButton
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 48
                        cornerRadius: Theme.radiusMd
                        active: Math.abs(panel.crop.ratio - modelData.ratio) < 1e-3
                        text: modelData.id
                        onClicked: {
                            App.editor.setCropRatio(modelData.id);
                            panel.cropRestarted();
                        }
                        contentItem: Column {
                            spacing: 4
                            topPadding: 6
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 14 / Math.max(1, ratioButton.modelData.ratio)
                                height: 14 * Math.min(1, ratioButton.modelData.ratio)
                                radius: 2
                                color: "transparent"
                                border.width: 2
                                border.color: ratioLabelText.color
                            }
                            Text {
                                id: ratioLabelText
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: ratioButton.modelData.id
                                font.pixelSize: Theme.fontSm
                                color: ratioButton.active ? Theme.brand400
                                     : ratioButton.hovered ? Theme.foreground : Theme.mutedForeground
                            }
                        }
                        Tip {
                            text: panel.ratioLabel(ratioButton.modelData.id)
                            visible: ratioButton.hovered
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.cropMode
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: panel.isInstagram
                    ? qsTr("Drag the frame to move it, the corners to resize, outside it to rotate (up to 45°; 90° in 1.91:1, like the normal crop; ← → 15° at a time). The frame keeps the Instagram format chosen above. C or Esc exits.")
                    : qsTr("Drag the frame to move it, the corners to resize, outside it to rotate; ← → rotate 15° at a time. The frame keeps the photo's proportions; at 90° it switches between portrait and landscape without turning the picture. C or Esc exits.")
                font.pixelSize: Theme.fontXs
                color: Theme.mutedForeground
                wrapMode: Text.Wrap
            }

            ValueSlider {
                Layout.fillWidth: true
                label: qsTr("Angle")
                from: -App.editor.cropMaxAngle
                to: App.editor.cropMaxAngle
                stepSize: 0.1
                value: panel.crop.angle
                showSign: false
                valueText: Number(value).toFixed(1) + "°"
                onMoved: (v) => panel.cropEdited(Object.assign({}, panel.crop, { angle: v }), "rotate")
                onReleased: panel.cropCommitted()
            }

            FormError {
                Layout.fillWidth: true
                text: panel.isInstagram && !App.editor.instagramFeedOk
                    ? qsTr("Past 45° the 1.91:1 frame becomes vertical (1:1.91), outside the formats the feed accepts: it is exported, but cannot be published.")
                    : ""
            }

            Button {
                Layout.fillWidth: true
                variant: "ghost"
                size: "sm"
                iconName: "rotate-ccw"
                text: qsTr("Reset crop")
                // Na versão Instagram "sem crop" não existe: o quadro volta
                // ao maior da proporção escolhida.
                disabled: panel.isInstagram
                    ? panel.crop.scale >= 0.9999 && panel.crop.angle === 0 && panel.crop.cx === 0.5 && panel.crop.cy === 0.5
                    : App.editor.cropIsIdentity
                onClicked: {
                    App.editor.resetCrop();
                    panel.cropRestarted();
                }
            }
        }
    }

    LayersPanel {
        Layout.fillWidth: true
        disabled: panel.disabled
        onSelectRequested: (layerId) => panel.selectRequested(layerId)
    }

    // Presets
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        SectionTitle {
            text: qsTr("Presets")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: App.editor.presets

                PillButton {
                    required property var modelData
                    text: panel.presetLabel(modelData.id)
                    active: App.editor.preset === modelData.id
                    onClicked: App.editor.togglePreset(modelData.id)
                }
            }
        }
    }

    // Luz / Cor
    Repeater {
        model: ["light", "color"]

        ColumnLayout {
            id: group
            required property string modelData
            Layout.fillWidth: true
            spacing: 0

            SectionTitle {
                Layout.bottomMargin: 4
                text: panel.groupLabel(group.modelData)
            }

            Repeater {
                model: App.editor.sliders.filter((s) => s.group === group.modelData)

                ValueSlider {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 52
                    label: panel.sliderLabel(modelData.name)
                    from: modelData.min
                    to: modelData.max
                    stepSize: modelData.step
                    value: panel.values[modelData.name] || 0
                    onMoved: (v) => App.editor.setValue(modelData.name, v)
                    onReleased: App.editor.commit()
                }
            }
        }
    }
}
