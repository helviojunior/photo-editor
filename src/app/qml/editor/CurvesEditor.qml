pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor
import "curve.js" as Curve

// Aba Curvas do painel (as Curves do Photoshop), da camada ativa: gráfico
// quadrado com grade 4×4, o histograma do canal ao fundo, a diagonal de
// referência e a curva — desenhada com a mesma Fritsch–Carlson do motor
// (curve.js), então o que se vê é o que a foto recebe.
//
//   - clicar numa área vazia cria um ponto (perto da curva, em cima dela);
//     até 16 pontos por canal
//   - arrastar move; x fica entre os vizinhos
//   - duplo clique, ou arrastar para fora do gráfico, remove — menos os dois
//     extremos, que só se movem (também na horizontal, até o vizinho)
//   - Entrada/Saída (0–255) do ponto sob o mouse ou arrastado
//
// Arrastar muda o rascunho (setCurve — a Editada acompanha); soltar grava
// (commit). Enquanto arrasta, a curva desenhada é a cópia local
// (``working``); ao soltar volta a ser a do App.editor.curves.
ColumnLayout {
    id: editor

    property string channel: "rgb"

    readonly property int maxPoints: 16
    /// Distância mínima em x entre dois pontos (o motor junta x repetidos).
    readonly property real minGap: 0.01
    readonly property var all: App.editor.curves
    /// Cópia local durante o arraste; null = a gravada/rascunho do motor.
    property var working: null
    readonly property var points: working !== null ? working : Curve.normalize(all[channel])
    readonly property color curveColor: channelColor(channel)

    /// Ponto em destaque (sob o mouse ou arrastado), -1 = nenhum.
    property int activeIndex: -1
    property bool dragging: false
    /// Arrastado para fora: sai ao soltar.
    property bool removing: false
    /// Posição do mouse sobre o gráfico (x em 0..1; -1 fora).
    property real hoverX: -1

    function channelColor(c) {
        return c === "r" ? Theme.channelRed : c === "g" ? Theme.channelGreen
             : c === "b" ? Theme.channelBlue : Theme.curveRgb;
    }
    function marked(c) {
        return !Curve.isIdentity(all[c]);
    }
    function copy(pts) {
        return pts.map((p) => [p[0], p[1]]);
    }
    function round4(v) {
        return Math.round(v * 10000) / 10000;
    }
    /// Grava uma curva na hora (remover ponto).
    function save(pts) {
        App.editor.setCurve(channel, pts);
        App.editor.commit();
    }

    onPointsChanged: plot.requestPaint()
    onChannelChanged: {
        working = null;
        activeIndex = -1;
    }

    spacing: 10

    Segmented {
        Layout.fillWidth: true
        accessibleName: qsTr("Channel")
        value: editor.channel
        options: [
            { value: "rgb", text: "RGB", tip: qsTr("All channels"), marked: editor.marked("rgb") },
            { value: "r", text: "R", tip: qsTr("Red"), accent: Theme.channelRed, marked: editor.marked("r") },
            { value: "g", text: "G", tip: qsTr("Green"), accent: Theme.channelGreen, marked: editor.marked("g") },
            { value: "b", text: "B", tip: qsTr("Blue"), accent: Theme.channelBlue, marked: editor.marked("b") }
        ]
        onActivated: (v) => editor.channel = v
    }

    Item {
        id: graph

        // Respiro para os quadradinhos dos extremos não cortarem.
        readonly property real pad: 6
        readonly property real span: width - 2 * pad

        function gx(x) {
            return pad + x * span;
        }
        function gy(y) {
            return pad + (1 - y) * span;
        }
        function fx(px) {
            return (px - pad) / span;
        }
        function fy(py) {
            return 1 - (py - pad) / span;
        }

        Layout.fillWidth: true
        implicitHeight: width
        Accessible.role: Accessible.Graphic
        Accessible.name: qsTr("Curve")

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.graphBackground
        }

        Histogram {
            x: graph.pad
            y: graph.pad
            width: graph.span
            height: graph.span
            background: false
            clipping: false
            strength: 0.45
            channel: editor.channel === "rgb" ? "l" : editor.channel
        }

        Canvas {
            id: plot

            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            Connections {
                target: editor
                function onActiveIndexChanged() { plot.requestPaint(); }
                function onRemovingChanged() { plot.requestPaint(); }
                function onAllChanged() { plot.requestPaint(); }
            }

            function drawCurve(ctx, pts, color, lineWidth) {
                const f = Curve.spline(pts);
                const steps = Math.max(32, Math.round(graph.span / 2));
                ctx.beginPath();
                for (let i = 0; i <= steps; ++i) {
                    const x = i / steps;
                    const px = graph.gx(x), py = graph.gy(f(x));
                    if (i === 0)
                        ctx.moveTo(px, py);
                    else
                        ctx.lineTo(px, py);
                }
                ctx.lineWidth = lineWidth;
                ctx.strokeStyle = color;
                ctx.stroke();
            }

            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const p = graph.pad, s = graph.span;
                // Grade 4×4 e a diagonal.
                ctx.lineWidth = 1;
                ctx.strokeStyle = Theme.css(Theme.graphGrid, -1);
                ctx.beginPath();
                for (let i = 0; i <= 4; ++i) {
                    const v = Math.round(p + s * i / 4) + 0.5;
                    ctx.moveTo(v, p);
                    ctx.lineTo(v, p + s);
                    ctx.moveTo(p, v);
                    ctx.lineTo(p + s, v);
                }
                ctx.stroke();
                ctx.strokeStyle = Theme.css(Theme.graphGuide, -1);
                ctx.beginPath();
                ctx.moveTo(graph.gx(0), graph.gy(0));
                ctx.lineTo(graph.gx(1), graph.gy(1));
                ctx.stroke();

                // No RGB, as curvas dos canais (se houver) aparecem apagadas.
                if (editor.channel === "rgb") {
                    for (const c of ["r", "g", "b"]) {
                        if (editor.marked(c))
                            drawCurve(ctx, Curve.normalize(editor.all[c]), Theme.css(editor.channelColor(c), 0.45), 1);
                    }
                }
                drawCurve(ctx, editor.points, Theme.css(editor.curveColor, 1), 2);

                // Pontos: quadradinhos; o ativo cheio, o que vai sair vazado.
                const pts = editor.points;
                for (let i = 0; i < pts.length; ++i) {
                    const px = graph.gx(pts[i][0]), py = graph.gy(pts[i][1]);
                    const active = i === editor.activeIndex;
                    const half = active ? 4.5 : 3.5;
                    ctx.fillStyle = Theme.css(active ? editor.curveColor : Theme.graphBackground, 1);
                    ctx.fillRect(px - half, py - half, half * 2, half * 2);
                    ctx.lineWidth = 1.5;
                    ctx.strokeStyle = Theme.css(editor.curveColor, 1);
                    ctx.strokeRect(px - half, py - half, half * 2, half * 2);
                }
            }
        }

        MouseArea {
            id: mouse

            // Ponto sendo arrastado: a curva sem ele + onde ele está.
            property var others: []
            property var dragPoint: null

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: editor.dragging ? Qt.ClosedHandCursor
                       : editor.activeIndex >= 0 ? Qt.OpenHandCursor : Qt.CrossCursor

            function nearest(mx, my) {
                const pts = editor.points;
                let best = -1, bestDist = 9;
                for (let i = 0; i < pts.length; ++i) {
                    const d = Math.hypot(graph.gx(pts[i][0]) - mx, graph.gy(pts[i][1]) - my);
                    if (d < bestDist) {
                        best = i;
                        bestDist = d;
                    }
                }
                return best;
            }
            function updateHover(mx, my) {
                const x = graph.fx(mx);
                editor.hoverX = x >= 0 && x <= 1 ? x : -1;
                if (!editor.dragging)
                    editor.activeIndex = nearest(mx, my);
            }

            onPressed: (event) => {
                const pts = Curve.normalize(editor.points);
                let index = nearest(event.x, event.y);
                if (index < 0) {
                    // Ponto novo: só dentro do gráfico, com vaga e longe dos vizinhos.
                    const x = graph.fx(event.x);
                    let y = graph.fy(event.y);
                    if (x <= 0 || x >= 1 || y < 0 || y > 1 || pts.length >= editor.maxPoints)
                        return;
                    if (pts.some((q) => Math.abs(q[0] - x) < editor.minGap))
                        return;
                    // Perto da curva: o ponto nasce em cima dela (não dá salto).
                    const onCurve = Curve.spline(pts)(x);
                    if (Math.abs(graph.gy(onCurve) - event.y) < 10)
                        y = onCurve;
                    index = pts.findIndex((q) => q[0] > x);
                    if (index < 0)
                        index = pts.length;
                    pts.splice(index, 0, [x, y]);
                }
                others = pts.filter((q, i) => i !== index);
                dragPoint = pts[index];
                editor.activeIndex = index;
                editor.dragging = true;
                editor.removing = false;
                editor.working = pts;
                App.editor.setCurve(editor.channel, pts);
            }
            onPositionChanged: (event) => {
                updateHover(event.x, event.y);
                if (!editor.dragging)
                    return;
                const index = editor.activeIndex;
                const n = others.length + 1;
                const endpoint = index === 0 || index === n - 1;
                // Longe do gráfico (20px): o ponto sai (menos os extremos).
                const out = event.x < -20 || event.y < -20 || event.x > width + 20 || event.y > height + 20;
                if (out && !endpoint) {
                    editor.removing = true;
                    editor.working = editor.copy(others);
                    App.editor.setCurve(editor.channel, editor.working);
                    return;
                }
                editor.removing = false;
                const lower = index === 0 ? 0 : others[index - 1][0] + editor.minGap;
                const upper = index === n - 1 ? 1 : others[index][0] - editor.minGap;
                const x = editor.round4(Math.min(upper, Math.max(lower, graph.fx(event.x))));
                const y = editor.round4(Math.min(1, Math.max(0, graph.fy(event.y))));
                dragPoint = [x, y];
                const pts = editor.copy(others);
                pts.splice(index, 0, dragPoint);
                editor.working = pts;
                App.editor.setCurve(editor.channel, pts);
            }
            function finish() {
                if (!editor.dragging)
                    return;
                editor.dragging = false;
                if (editor.removing)
                    editor.activeIndex = -1;
                editor.removing = false;
                App.editor.commit();
                editor.working = null;
            }
            onReleased: finish()
            onCanceled: finish()
            onExited: {
                if (!editor.dragging) {
                    editor.activeIndex = -1;
                    editor.hoverX = -1;
                }
            }
            onDoubleClicked: (event) => {
                const pts = Curve.normalize(editor.points);
                const index = nearest(event.x, event.y);
                if (index <= 0 || index >= pts.length - 1)
                    return;
                pts.splice(index, 1);
                editor.activeIndex = -1;
                editor.save(pts);
            }
        }
    }

    // Entrada/Saída do ponto em destaque (ou da curva sob o mouse).
    RowLayout {
        id: readoutRow

        Layout.fillWidth: true
        spacing: 12

        readonly property var readout: {
            const pts = editor.points;
            if (editor.activeIndex >= 0 && editor.activeIndex < pts.length && !editor.removing)
                return pts[editor.activeIndex];
            if (editor.hoverX >= 0)
                return [editor.hoverX, Curve.spline(pts)(editor.hoverX)];
            return null;
        }

        Text {
            text: qsTr("Input: %1").arg(readoutRow.readout ? Math.round(readoutRow.readout[0] * 255) : "–")
            font.pixelSize: Theme.fontSm
            font.features: { "tnum": 1 }
            color: Theme.foreground
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Output: %1").arg(readoutRow.readout ? Math.round(readoutRow.readout[1] * 255) : "–")
            font.pixelSize: Theme.fontSm
            font.features: { "tnum": 1 }
            color: Theme.foreground
        }
        Text {
            text: qsTr("%1/%2 points").arg(editor.points.length).arg(editor.maxPoints)
            font.pixelSize: Theme.fontXs
            font.features: { "tnum": 1 }
            color: Theme.mutedForeground
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Click to add a point, drag to move it. Double-click or drag it out of the graph to remove it; the two ends only move.")
        font.pixelSize: Theme.fontXs
        color: Theme.mutedForeground
        wrapMode: Text.Wrap
    }

    Button {
        Layout.fillWidth: true
        variant: "outline"
        size: "sm"
        iconName: "rotate-ccw"
        text: qsTr("Reset channel")
        disabled: !editor.marked(editor.channel)
        onClicked: App.editor.resetCurve(editor.channel)
    }
}
