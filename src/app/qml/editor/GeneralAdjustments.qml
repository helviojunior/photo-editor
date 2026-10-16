pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Aba "Ajustes" do painel (o Basic do Lightroom), da camada ativa:
//
//   histograma da Editada + dados de disparo (ISO, focal, abertura, velocidade)
//   Auto | Redefinir
//   presets — botões de uma vez só: SOMAM os deslocamentos deles aos sliders
//             (applyPreset), que continuam ajustáveis um a um; não ficam "ligados"
//   balanço de branco — conta-gotas (clique numa área branca/neutra da
//             Original) e Temperatura/Matiz com o trilho colorido
//   Luz — exposição, contraste, realces, sombras, brancos, pretos
//   Cor — vibração e saturação
//
// Sliders: arrastar muda o rascunho (setValue), soltar grava (commit); duplo
// clique no rótulo volta a zero.
ColumnLayout {
    id: general

    /// Conta-gotas ligado (o estado é do EditorPage, que liga o cursor na Original).
    property bool whitePicking: false
    signal whitePickToggled()

    readonly property var values: App.editor.values
    readonly property var info: App.editor.shootingInfo
    readonly property string legacyPreset: App.editor.preset

    function spec(name) {
        return App.editor.sliders.find((s) => s.name === name) || { min: -100, max: 100, step: 1 };
    }
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
    /// "ISO 250   39 mm   f/4   1/200" — só o que o EXIF tem.
    function shootingLine(i) {
        const parts = [];
        if (i.iso !== undefined)
            parts.push(qsTr("ISO %1").arg(i.iso));
        if (i.focalLength !== undefined)
            parts.push(qsTr("%1 mm").arg(Math.round(i.focalLength * 10) / 10));
        if (i.aperture !== undefined)
            parts.push(i.aperture);
        if (i.shutter !== undefined)
            parts.push(i.shutter);
        return parts.join("   ");
    }

    spacing: 16

    // Um slider do motor pelo nome (limites do App.editor.sliders).
    component NamedSlider: ValueSlider {
        required property string name
        readonly property var spec: general.spec(name)

        Layout.fillWidth: true
        implicitHeight: 52
        label: general.sliderLabel(name)
        from: spec.min
        to: spec.max
        stepSize: spec.step
        value: general.values[name] || 0
        onMoved: (v) => App.editor.setValue(name, v)
        onReleased: App.editor.commit()
    }

    // Histograma + dados de disparo
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        Histogram {
            Layout.fillWidth: true
            implicitHeight: 96
        }
        Text {
            id: shooting
            Layout.fillWidth: true
            visible: text !== ""
            text: general.shootingLine(general.info)
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSm
            font.features: { "tnum": 1 }
            color: Theme.mutedForeground
            elide: Text.ElideRight

            readonly property string gear: [general.info.camera, general.info.lens]
                .filter((s) => s !== undefined && s !== "").join("\n")

            MouseArea {
                id: shootingMouse
                anchors.fill: parent
                hoverEnabled: true
            }
            Tip {
                text: shooting.gear
                visible: shootingMouse.containsMouse && text !== ""
            }
        }
    }

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
                text: qsTr("Auto") + Keymap.hint("auto")
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

    // Presets: aplicam uma vez, nos sliders.
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
                    id: presetButton
                    required property var modelData
                    text: Messages.presetName(modelData.id)
                    Accessible.checkable: false
                    onClicked: App.editor.applyPreset(modelData.id)

                    Tip {
                        text: qsTr("Applies the preset to the sliders below — fine-tune from there")
                        visible: presetButton.hovered
                    }
                }
            }
        }
        // Foto de antes: o preset ficava "ligado" por cima dos sliders.
        RowLayout {
            Layout.fillWidth: true
            visible: general.legacyPreset !== ""
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: qsTr("Older preset still on top of the sliders: %1").arg(Messages.presetName(general.legacyPreset))
                font.pixelSize: Theme.fontXs
                color: Theme.mutedForeground
                wrapMode: Text.Wrap
            }
            SmallIconButton {
                iconName: "x"
                tooltip: qsTr("Remove the older preset")
                onClicked: App.editor.togglePreset(general.legacyPreset)
            }
        }
    }

    // Balanço de branco
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            spacing: 8

            SectionTitle {
                Layout.fillWidth: true
                text: qsTr("White balance")
            }
            IconButton {
                iconName: "pipette"
                diameter: 28
                iconSize: 16
                active: general.whitePicking
                tooltip: general.whitePicking ? qsTr("Cancel the white balance picker")
                                              : qsTr("Select white balance point")
                shortcut: Keymap.label("whitePicker")
                Accessible.checkable: true
                Accessible.checked: general.whitePicking
                onClicked: general.whitePickToggled()
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            visible: general.whitePicking
            text: qsTr("Click a white or neutral gray area of the Original photo. Esc cancels.")
            font.pixelSize: Theme.fontXs
            color: Theme.brandForeground
            wrapMode: Text.Wrap
        }

        NamedSlider {
            name: "temperature"
            trackGradient: [Theme.trackTempCool, Theme.trackTempWarm]
        }
        NamedSlider {
            name: "tint"
            trackGradient: [Theme.trackTintGreen, Theme.trackTintMagenta]
        }
    }

    // Luz
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        SectionTitle {
            Layout.bottomMargin: 4
            text: qsTr("Light")
        }
        Repeater {
            model: App.editor.sliders.filter((s) => s.group === "light")

            NamedSlider {
                required property var modelData
                name: modelData.name
            }
        }
    }

    // Cor
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        SectionTitle {
            Layout.bottomMargin: 4
            text: qsTr("Color")
        }
        Repeater {
            model: App.editor.sliders.filter((s) => s.group === "color" && s.name !== "temperature" && s.name !== "tint")

            NamedSlider {
                required property var modelData
                name: modelData.name
                trackGradient: [Theme.trackSatLow, Theme.trackSatMid, Theme.trackSatHigh]
            }
        }
    }
}
