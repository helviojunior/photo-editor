pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtCore
import PhotoEditor

// Painel de edição (EditPanel.jsx): crop, camadas e, em abas, os ajustes da
// camada ativa — Ajustes (histograma, Auto, presets, balanço de branco,
// luz e cor), Níveis, Curvas e Efeitos. A aba escolhida fica lembrada
// (QSettings, "editPanel/tab"); Alt+1..4 trocam (Keymap tabGeneral...).
//
// Na versão Instagram o crop ganha a escolha da proporção — 4:5, 1:1 ou
// 1,91:1 — e o giro fica no limite dela: o quadro nunca sai do que o feed aceita.
//
// Tudo nas abas edita a camada ATIVA (App.editor.activeLayer — o nome dela
// aparece logo abaixo das abas); sem camada ativa, os ajustes da própria
// foto. Arrastar muda o rascunho (a "Editada" acompanha); soltar grava
// (commit) — uma entrada no histórico por gesto, não uma por pixel arrastado.
//
// O crop e o conta-gotas são do EditorPage (é ele que mexe na Original): o
// painel pede por sinais.
ColumnLayout {
    id: panel

    property bool cropMode: false
    property bool disabled: false
    /// Conta-gotas do balanço de branco ligado (estado do EditorPage).
    property bool whitePicking: false
    /// Aba atual: "general" | "levels" | "curves" | "effects".
    property string tab: "general"
    readonly property var tabs: ["general", "levels", "curves", "effects"]

    signal cropToggled()
    /// Mudança do quadro: mode "rotate" | "ratio" | "reset".
    signal cropEdited(var crop, string mode)
    signal cropCommitted()
    /// O quadro mudou de tamanho por fora (proporção, desfazer recorte).
    signal cropRestarted()
    signal selectRequested(string layerId)
    signal whitePickToggled()

    property Settings store: Settings {
        category: "editPanel"
        property alias tab: panel.tab
    }
    Component.onCompleted: {
        if (!panel.tabs.includes(panel.tab))
            panel.tab = "general";
    }

    readonly property var current: App.editor.current
    readonly property bool isInstagram: current.isInstagram === true
    readonly property var crop: App.editor.crop

    function ratioLabel(id) {
        switch (id) {
        case "4:5": return qsTr("4:5 — portrait (1080×1350)");
        case "1:1": return qsTr("1:1 — square (1080×1080)");
        case "1.91:1": return qsTr("1.91:1 — landscape (1080×566)");
        }
        return id;
    }

    readonly property string layerName: {
        const layers = App.editor.layers;
        if (App.editor.activeLayer === "")
            return layers.length ? qsTr("Rest of the photo") : qsTr("Whole photo");
        const i = layers.findIndex((l) => l.id === App.editor.activeLayer);
        return qsTr("Selection %1").arg(i + 1);
    }

    spacing: 16
    enabled: !disabled
    opacity: disabled ? 0.6 : 1

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
                text: qsTr("Crop mode") + Keymap.hint("crop")
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

    // Abas
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        Segmented {
            Layout.fillWidth: true
            segmentHeight: 30
            accessibleName: qsTr("Adjustments")
            value: panel.tab
            options: [
                { value: "general", text: qsTr("General"), tip: qsTr("General adjustments") + Keymap.hint("tabGeneral") },
                { value: "levels", text: qsTr("Levels"), tip: qsTr("Levels") + Keymap.hint("tabLevels") },
                { value: "curves", text: qsTr("Curves"), tip: qsTr("Curves") + Keymap.hint("tabCurves") },
                { value: "effects", text: qsTr("Effects"), tip: qsTr("Effects library") + Keymap.hint("tabEffects"),
                  marked: App.editor.effects.length > 0 }
            ]
            onActivated: (v) => panel.tab = v
        }

        // A camada que as abas editam.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Icon {
                name: "layers"
                size: 12
                color: App.editor.activeLayer !== "" ? Theme.brand400 : Theme.mutedForeground
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Editing: %1").arg(panel.layerName)
                font.pixelSize: Theme.fontXs
                color: App.editor.activeLayer !== "" ? Theme.foreground : Theme.mutedForeground
                elide: Text.ElideRight
            }
        }
    }

    GeneralAdjustments {
        Layout.fillWidth: true
        visible: panel.tab === "general"
        whitePicking: panel.whitePicking
        onWhitePickToggled: panel.whitePickToggled()
    }
    LevelsEditor {
        Layout.fillWidth: true
        visible: panel.tab === "levels"
    }
    CurvesEditor {
        Layout.fillWidth: true
        visible: panel.tab === "curves"
    }
    EffectsLibrary {
        Layout.fillWidth: true
        visible: panel.tab === "effects"
    }
}
