import QtQuick
import PhotoEditor

// Histograma da "Editada" (App.editor.histogram, calculado fora da thread
// principal): R, G e B sobrepostos e somados — onde os três coincidem dá
// branco, como no Lightroom — por cima da luminância em cinza.
//
//   Histogram { Layout.fillWidth: true; implicitHeight: 96 }        // completo
//   Histogram { channel: "r"; background: false; strength: 0.35 }   // fundo das Curvas
//
// - ``channel``: "all" (R+G+B+luminância) | "l" | "r" | "g" | "b".
// - ``clipping``: os triângulos dos cantos acendem com mais de 0,1% dos pixels
//   recortados nas sombras (algum canal em 0) ou nas altas luzes (em 255).
// - Os valores já vêm normalizados (raiz da contagem, 0..1): é só desenhar.
Item {
    id: root

    property var histogram: App.editor.histogram
    property string channel: "all"
    property bool clipping: true
    /// Fundo escuro próprio (desligue quando o gráfico vai por baixo de outro).
    property bool background: true
    /// Opacidade das áreas (menor = histograma de fundo, discreto).
    property real strength: 1

    readonly property bool hasData: histogram !== undefined && histogram !== null && histogram.l !== undefined
    readonly property real clipShadows: hasData ? Number(histogram.clipShadows || 0) : 0
    readonly property real clipHighlights: hasData ? Number(histogram.clipHighlights || 0) : 0
    readonly property real clipThreshold: 0.001

    function percent(v) {
        return (v * 100).toFixed(v < 0.01 ? 2 : 1);
    }

    implicitHeight: 96
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Histogram")

    onHistogramChanged: graph.requestPaint()
    onChannelChanged: graph.requestPaint()
    onStrengthChanged: graph.requestPaint()

    Rectangle {
        anchors.fill: parent
        visible: root.background
        radius: Theme.radiusMd
        color: Theme.graphBackground
    }

    Canvas {
        id: graph

        anchors.fill: parent
        anchors.margins: root.background ? 1 : 0
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function area(ctx, values, fill) {
            const w = width, h = height;
            const n = values.length;
            if (n < 2)
                return;
            ctx.beginPath();
            ctx.moveTo(0, h);
            for (let i = 0; i < n; ++i)
                ctx.lineTo(i / (n - 1) * w, h - Math.min(1, Math.max(0, values[i])) * (h - 2));
            ctx.lineTo(w, h);
            ctx.closePath();
            ctx.fillStyle = fill;
            ctx.fill();
        }

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!root.hasData)
                return;
            const d = root.histogram;
            const s = root.strength;
            if (root.channel === "all") {
                ctx.globalCompositeOperation = "source-over";
                area(ctx, d.l || [], Theme.css(Theme.channelLuma, 0.25 * s));
                // Somados: R+G = amarelo, R+G+B = branco.
                ctx.globalCompositeOperation = "lighter";
                area(ctx, d.r || [], Theme.css(Theme.channelRed, 0.42 * s));
                area(ctx, d.g || [], Theme.css(Theme.channelGreen, 0.42 * s));
                area(ctx, d.b || [], Theme.css(Theme.channelBlue, 0.42 * s));
                ctx.globalCompositeOperation = "source-over";
            } else {
                const c = root.channel === "r" ? Theme.channelRed : root.channel === "g" ? Theme.channelGreen
                        : root.channel === "b" ? Theme.channelBlue : Theme.channelLuma;
                area(ctx, d[root.channel] || [], Theme.css(c, 0.6 * s));
            }
        }
    }

    // Recorte: sombras à esquerda, altas luzes à direita.
    component ClipMark: Item {
        id: mark

        property bool lit: false
        property bool rightSide: false
        property string tip: ""

        width: 14
        height: 14
        onLitChanged: tri.requestPaint()

        Canvas {
            id: tri
            anchors.fill: parent
            anchors.margins: 3
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.beginPath();
                if (mark.rightSide) {
                    ctx.moveTo(width, 0);
                    ctx.lineTo(width, height);
                    ctx.lineTo(0, 0);
                } else {
                    ctx.moveTo(0, 0);
                    ctx.lineTo(width, 0);
                    ctx.lineTo(0, height);
                }
                ctx.closePath();
                ctx.fillStyle = Theme.css(mark.lit ? Theme.clipOn : Theme.clipOff, -1);
                ctx.fill();
            }
        }
        MouseArea {
            id: markMouse
            anchors.fill: parent
            hoverEnabled: true
        }
        Tip {
            text: mark.tip
            visible: markMouse.containsMouse
        }
    }

    ClipMark {
        visible: root.clipping
        x: 2
        y: 2
        lit: root.clipShadows > root.clipThreshold
        tip: qsTr("Clipped shadows: %1%").arg(root.percent(root.clipShadows))
    }
    ClipMark {
        visible: root.clipping
        x: root.width - width - 2
        y: 2
        rightSide: true
        lit: root.clipHighlights > root.clipThreshold
        tip: qsTr("Clipped highlights: %1%").arg(root.percent(root.clipHighlights))
    }
}
