pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Aba Níveis do painel (os Levels do Photoshop), da camada ativa:
//
//   canal       RGB | R | G | B
//   entrada     histograma do canal com três triângulos — preto, gama
//               (meio-tom) e branco — e os três números
//   saída       faixa preto → branco (ou → a cor do canal) com dois
//               triângulos e os dois números
//   Auto / Redefinir o canal
//
// Arrastar muda o rascunho (setLevels — a Editada acompanha); soltar grava
// (commit). Números e Auto/Redefinir gravam na hora.
//
// O triângulo do gama fica onde o meio-tom de saída (50%) cai na entrada,
// como no Photoshop. O motor faz v' = v^(1/gama) com v já normalizado entre
// preto e branco (imaging/Look.cpp, applyLevels), então v' = 0,5 em
// v = 0,5^gama:
//
//   posição = inBlack + (inWhite - inBlack) · 0,5^gama
//   gama    = ln(fração) / ln(0,5),  fração = (posição - inBlack) / (inWhite - inBlack)
//
// Gama 1 = no meio; gama > 1 clareia e leva o triângulo para a esquerda.
// Mexer no preto ou no branco arrasta o gama junto (a fração fica).
ColumnLayout {
    id: editor

    property string channel: "rgb"

    readonly property var all: App.editor.levels
    readonly property var lv: all[channel] || ({ inBlack: 0, gamma: 1, inWhite: 255, outBlack: 0, outWhite: 255 })
    readonly property color channelColor: channel === "r" ? Theme.channelRed : channel === "g" ? Theme.channelGreen
                                        : channel === "b" ? Theme.channelBlue : Theme.white

    readonly property real gammaMin: 0.10
    readonly property real gammaMax: 9.99

    function isIdentity(l) {
        return !l || (l.inBlack === 0 && Math.abs(l.gamma - 1) < 1e-9 && l.inWhite === 255
                      && l.outBlack === 0 && l.outWhite === 255);
    }
    function gammaPosition(l) {
        return l.inBlack + (l.inWhite - l.inBlack) * Math.pow(0.5, l.gamma);
    }
    function gammaFor(position, l) {
        const span = l.inWhite - l.inBlack;
        const fraction = Math.min(0.999, Math.max(0.001, (position - l.inBlack) / span));
        const g = Math.log(fraction) / Math.log(0.5);
        return Math.round(Math.min(gammaMax, Math.max(gammaMin, g)) * 100) / 100;
    }
    /// Rascunho (arrastar).
    function draft(change) {
        App.editor.setLevels(channel, change);
    }
    /// Grava na hora (números).
    function apply(change) {
        App.editor.setLevels(channel, change);
        App.editor.commit();
    }

    spacing: 10

    Segmented {
        Layout.fillWidth: true
        accessibleName: qsTr("Channel")
        value: editor.channel
        options: [
            { value: "rgb", text: "RGB", tip: qsTr("All channels"), marked: !editor.isIdentity(editor.all.rgb) },
            { value: "r", text: "R", tip: qsTr("Red"), accent: Theme.channelRed, marked: !editor.isIdentity(editor.all.r) },
            { value: "g", text: "G", tip: qsTr("Green"), accent: Theme.channelGreen, marked: !editor.isIdentity(editor.all.g) },
            { value: "b", text: "B", tip: qsTr("Blue"), accent: Theme.channelBlue, marked: !editor.isIdentity(editor.all.b) }
        ]
        onActivated: (v) => editor.channel = v
    }

    // --- Entrada ----------------------------------------------------------- //
    SectionTitle {
        text: qsTr("Input levels")
    }

    Item {
        id: input

        // O que o triângulo arrastado é: "black" | "gamma" | "white" | "".
        property string dragging: ""

        Layout.fillWidth: true
        implicitHeight: inputGraph.height + 4 + 11

        function xOf(v) {
            return v / 255 * width;
        }
        function valueAt(x) {
            return Math.min(255, Math.max(0, x / width * 255));
        }

        Histogram {
            id: inputGraph
            width: parent.width
            height: 92
            channel: editor.channel === "rgb" ? "l" : editor.channel
            clipping: false
        }
        // Fora do intervalo de entrada: apagado (vira preto puro / branco puro).
        Rectangle {
            x: 1
            y: 1
            width: Math.max(0, input.xOf(editor.lv.inBlack) - 1)
            height: inputGraph.height - 2
            color: Theme.alpha(Theme.black, 0.45)
        }
        Rectangle {
            x: input.xOf(editor.lv.inWhite)
            y: 1
            width: Math.max(0, input.width - x - 1)
            height: inputGraph.height - 2
            color: Theme.alpha(Theme.black, 0.45)
        }

        LevelHandle {
            y: inputGraph.height + 4
            position: input.xOf(editor.lv.inBlack)
            fill: Theme.black
            stroke: Theme.mutedForeground
            active: input.dragging === "black"
        }
        LevelHandle {
            y: inputGraph.height + 4
            position: input.xOf(editor.gammaPosition(editor.lv))
            fill: Theme.levelsMid
            stroke: Theme.mutedForeground
            active: input.dragging === "gamma"
        }
        LevelHandle {
            y: inputGraph.height + 4
            position: input.xOf(editor.lv.inWhite)
            fill: Theme.white
            stroke: Theme.mutedForeground
            active: input.dragging === "white"
        }

        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            preventStealing: true
            cursorShape: input.dragging !== "" ? Qt.SizeHorCursor : Qt.PointingHandCursor

            function local(mx) {
                return mx - 6;
            }
            onPressed: (event) => {
                const x = local(event.x);
                const l = editor.lv;
                const d = [
                    { id: "gamma", dist: Math.abs(x - input.xOf(editor.gammaPosition(l))) },
                    { id: "black", dist: Math.abs(x - input.xOf(l.inBlack)) },
                    { id: "white", dist: Math.abs(x - input.xOf(l.inWhite)) }
                ];
                // Preto e branco juntos: o lado do clique decide.
                d.sort((a, b) => a.dist - b.dist);
                input.dragging = d[0].id;
                move(x);
            }
            onPositionChanged: (event) => {
                if (input.dragging !== "")
                    move(local(event.x));
            }
            function move(x) {
                const l = editor.lv;
                const v = input.valueAt(x);
                if (input.dragging === "black")
                    editor.draft({ inBlack: Math.round(Math.min(v, l.inWhite - 2)) });
                else if (input.dragging === "white")
                    editor.draft({ inWhite: Math.round(Math.max(v, l.inBlack + 2)) });
                else if (input.dragging === "gamma")
                    editor.draft({ gamma: editor.gammaFor(v, l) });
            }
            function finish() {
                if (input.dragging === "")
                    return;
                input.dragging = "";
                App.editor.commit();
            }
            onReleased: finish()
            onCanceled: finish()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        NumberField {
            from: 0
            to: 253
            value: editor.lv.inBlack
            Accessible.name: qsTr("Input black")
            onEdited: (v) => editor.apply({ inBlack: Math.round(Math.min(v, editor.lv.inWhite - 2)) })
        }
        Item { Layout.fillWidth: true }
        NumberField {
            from: editor.gammaMin
            to: editor.gammaMax
            decimals: 2
            value: editor.lv.gamma
            Accessible.name: qsTr("Gamma (midtones)")
            onEdited: (v) => editor.apply({ gamma: v })
        }
        Item { Layout.fillWidth: true }
        NumberField {
            from: 2
            to: 255
            value: editor.lv.inWhite
            Accessible.name: qsTr("Input white")
            onEdited: (v) => editor.apply({ inWhite: Math.round(Math.max(v, editor.lv.inBlack + 2)) })
        }
    }

    // --- Saída ------------------------------------------------------------- //
    SectionTitle {
        Layout.topMargin: 4
        text: qsTr("Output levels")
    }

    Item {
        id: output

        property string dragging: ""

        Layout.fillWidth: true
        implicitHeight: bar.height + 4 + 11

        function xOf(v) {
            return v / 255 * width;
        }

        Rectangle {
            id: bar
            width: parent.width
            height: 14
            radius: Theme.radiusSm
            border.color: Theme.border
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.black }
                GradientStop { position: 1; color: editor.channelColor }
            }
        }
        LevelHandle {
            y: bar.height + 4
            position: output.xOf(editor.lv.outBlack)
            fill: Theme.black
            stroke: Theme.mutedForeground
            active: output.dragging === "black"
        }
        LevelHandle {
            y: bar.height + 4
            position: output.xOf(editor.lv.outWhite)
            fill: Theme.white
            stroke: Theme.mutedForeground
            active: output.dragging === "white"
        }

        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            preventStealing: true
            cursorShape: output.dragging !== "" ? Qt.SizeHorCursor : Qt.PointingHandCursor

            function valueAt(mx) {
                return Math.round(Math.min(255, Math.max(0, (mx - 6) / output.width * 255)));
            }
            onPressed: (event) => {
                const v = valueAt(event.x);
                const l = editor.lv;
                const db = Math.abs(v - l.outBlack), dw = Math.abs(v - l.outWhite);
                // Juntos: o lado do clique decide (à direita do par, o branco).
                output.dragging = db < dw || (db === dw && v < l.outBlack) ? "black" : "white";
                move(v);
            }
            onPositionChanged: (event) => {
                if (output.dragging !== "")
                    move(valueAt(event.x));
            }
            function move(v) {
                editor.draft(output.dragging === "black" ? { outBlack: v } : { outWhite: v });
            }
            function finish() {
                if (output.dragging === "")
                    return;
                output.dragging = "";
                App.editor.commit();
            }
            onReleased: finish()
            onCanceled: finish()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        NumberField {
            from: 0
            to: 255
            value: editor.lv.outBlack
            Accessible.name: qsTr("Output black")
            onEdited: (v) => editor.apply({ outBlack: Math.round(v) })
        }
        Item { Layout.fillWidth: true }
        NumberField {
            from: 0
            to: 255
            value: editor.lv.outWhite
            Accessible.name: qsTr("Output white")
            onEdited: (v) => editor.apply({ outWhite: Math.round(v) })
        }
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        columns: 2
        columnSpacing: 8

        Button {
            id: autoLevels
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            variant: "outline"
            size: "sm"
            iconName: "wand-sparkles"
            text: qsTr("Auto")
            onClicked: App.editor.autoLevels(editor.channel)

            Tip {
                text: qsTr("Sets black and white points from the histogram of this channel (0.1% clipped on each end)")
                visible: autoLevels.hovered
            }
        }
        Button {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            variant: "outline"
            size: "sm"
            iconName: "rotate-ccw"
            text: qsTr("Reset channel")
            disabled: editor.isIdentity(editor.lv)
            onClicked: App.editor.resetLevels(editor.channel)
        }
    }
}
