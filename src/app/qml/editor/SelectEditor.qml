import QtQuick
import PhotoEditor

// Seleção por pincel sobre a foto (SelectEditor.jsx) — o painel da esquerda
// no modo seleção do editor e na área de cada camada do merge.
//
// A pessoa pinta por cima do objeto e, ao soltar, o traço sai por
// ``stroked(points, radius, mode)`` — pontos em fração da foto inteira, raio
// em fração do lado maior (o mesmo tamanho em qualquer tela). Quem usa manda
// para App.editor.stroke / App.merge.stroke, que devolve a máscara nova. O
// traço fica desenhado no canvas até ela chegar. Alt força subtrair.
//
// Sem ``editable`` só mostra a foto com a máscara (mais apagada com
// ``faint``) — é o que se vê com uma camada ativa, ou a foto pura sem máscara.
//
//   SelectEditor {
//       label: qsTr("Original"); source: App.editor.originalUrl
//       aspect: App.editor.current.aspect
//       maskUrl: App.editor.selectionMaskUrl
//       editable: true; brush: brush; busy: App.editor.selectionBusy
//       onStroked: (points, radius, mode) => App.editor.stroke(points, radius, mode, brush.smart)
//   }
Rectangle {
    id: pane

    property string label: ""
    property url source
    /// Altura/largura da foto, até a imagem carregar (depois vale a dela).
    property real aspect: 1
    property string maskUrl: ""
    property bool faint: false
    property bool editable: false
    property bool busy: false
    property Brush brush: null

    signal stroked(var points, real radius, string mode)

    readonly property real photoAspect: photo.sourceAspect > 0 ? photo.sourceAspect : (aspect > 0 ? aspect : 1)
    // Maior caixa na proporção da foto que cabe com 8px de respiro (useFitSize).
    readonly property real boxWidth: Math.max(0, Math.min(width - 16, (height - 16) / photoAspect))
    readonly property real boxHeight: boxWidth * photoAspect
    readonly property real radiusPx: (brush ? brush.size : 0) * Math.max(boxWidth, boxHeight)

    color: Theme.imagePane
    clip: true

    PaneCaption {
        text: pane.label
        maximumWidth: pane.width - 48
    }

    Spinner {
        visible: pane.busy
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        z: 20
        color: Qt.rgba(1, 1, 1, 0.9)
    }

    Item {
        id: box
        anchors.centerIn: parent
        width: pane.boxWidth
        height: pane.boxHeight

        BufferedImage {
            id: photo
            anchors.fill: parent
            source: pane.source
        }

        MaskOverlay {
            id: overlay
            anchors.fill: parent
            url: pane.maskUrl
            faint: pane.faint
        }

        // O traço em andamento (e os já enviados, até a máscara chegar).
        Canvas {
            id: canvas

            property var ops: []
            property bool wipe: false

            function add(op) {
                ops.push(op);
                requestPaint();
            }
            function clear() {
                ops = [];
                wipe = true;
                requestPaint();
            }

            anchors.fill: parent
            visible: pane.editable
            opacity: 0.6
            onWidthChanged: clear()
            onHeightChanged: clear()

            onPaint: {
                const ctx = getContext("2d");
                if (wipe) {
                    ctx.clearRect(0, 0, width, height);
                    wipe = false;
                }
                for (const op of ops) {
                    ctx.strokeStyle = op.color;
                    ctx.fillStyle = op.color;
                    ctx.lineWidth = op.radius * 2;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";
                    ctx.beginPath();
                    if (op.from) {
                        ctx.moveTo(op.from[0], op.from[1]);
                        ctx.lineTo(op.to[0], op.to[1]);
                        ctx.stroke();
                    } else {
                        ctx.arc(op.to[0], op.to[1], op.radius, 0, Math.PI * 2, false);
                        ctx.fill();
                    }
                }
                ops = [];
            }
        }

        // Terminou tudo o que estava na fila e a máscara nova já está na
        // tela: o traço desenhado sai.
        readonly property bool settled: !pane.busy && !overlay.loading
        onSettledChanged: {
            if (settled)
                canvas.clear();
        }

        // O círculo do pincel no lugar do cursor.
        Rectangle {
            visible: pane.editable && mouse.containsMouse
            x: mouse.mouseX - pane.radiusPx - 1
            y: mouse.mouseY - pane.radiusPx - 1
            width: pane.radiusPx * 2 + 2
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.6)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.white
            }
        }

        MouseArea {
            id: mouse

            property var current: null

            function color(mode) {
                return mode === "subtract" ? "rgba(255, 255, 255, 0.6)" : Theme.hex(Theme.brand500);
            }

            anchors.fill: parent
            enabled: pane.editable
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton
            cursorShape: pane.editable ? Qt.BlankCursor : Qt.ArrowCursor

            onPressed: (event) => {
                if (pane.boxWidth <= 0 || !pane.brush)
                    return;
                const mode = (event.modifiers & Qt.AltModifier) ? "subtract" : pane.brush.mode;
                const p = [event.x, event.y];
                current = { mode: mode, points: [p] };
                canvas.add({ to: p, radius: pane.radiusPx, color: color(mode) });
            }
            onPositionChanged: (event) => {
                const s = current;
                if (!s)
                    return;
                const p = [event.x, event.y];
                const last = s.points[s.points.length - 1];
                if (Math.hypot(p[0] - last[0], p[1] - last[1]) < 3)
                    return;
                canvas.add({ from: last, to: p, radius: pane.radiusPx, color: color(s.mode) });
                s.points.push(p);
            }
            function finish() {
                const s = current;
                current = null;
                if (!s || pane.boxWidth <= 0)
                    return;
                pane.stroked(s.points.map((p) => [p[0] / pane.boxWidth, p[1] / pane.boxHeight]),
                             pane.brush.size, s.mode);
            }
            onReleased: finish()
            onCanceled: finish()
        }
    }
}
