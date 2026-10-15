pragma ComponentBehavior: Bound
import QtQuick
import PhotoEditor

// Modo crop sobre a foto ORIGINAL (CropEditor.jsx): a foto fica PARADA e só o
// quadro se mexe; a "Editada" ao lado mostra o recorte ao vivo.
//
//   arrastar dentro do quadro ...... move
//   arrastar um canto .............. redimensiona com o canto oposto fixo
//                                    (na proporção do quadro: a da foto, ou a
//                                    do Instagram na versão dele)
//   arrastar fora do quadro ........ gira o quadro em torno do centro dele
//
// ``edited(crop, mode)`` diz o gesto ("move" | "resize" | "rotate"): o
// EditorPage usa isso para o giro não encolher o quadro de vez (baseScale).
// Soltar emite ``committed()`` (grava). Toda mudança passa por
// App.editor.normalizeCrop, que mantém o quadro dentro da foto.
//
// O quadro é um Item girado sobre a imagem; a borda de 9999px dele, recortada
// pela caixa da foto, escurece o que fica de fora.
Rectangle {
    id: pane

    property string label: ""
    property url source
    /// Altura/largura da foto.
    property real aspect: 1
    /// {scale, cx, cy, angle, ratio}
    property var crop: ({ scale: 1, cx: 0.5, cy: 0.5, angle: 0, ratio: 0 })

    signal edited(var crop, string mode)
    signal committed()

    // Quadro com scale 1, em fração da largura e da altura da foto (crop.js: cropFrame).
    function cropFrame(ratio, a) {
        if (!ratio || ratio <= 0)
            return [1, 1];
        return [Math.min(1, a / ratio), Math.min(1, ratio / a)];
    }

    readonly property real photoAspect: aspect > 0 ? aspect : 1
    readonly property real boxWidth: Math.max(0, Math.min(width - 16, (height - 16) / photoAspect))
    readonly property real boxHeight: boxWidth * photoAspect
    readonly property var base: cropFrame(crop.ratio, photoAspect)
    // Quadro com scale 1, em px da foto na tela.
    readonly property real fullWidth: base[0] * boxWidth
    readonly property real fullHeight: base[1] * boxHeight

    color: Theme.imagePane
    clip: true

    PaneCaption {
        text: pane.label
        maximumWidth: pane.width - 16
    }

    Item {
        id: box
        anchors.centerIn: parent
        width: pane.boxWidth
        height: pane.boxHeight
        clip: true

        BufferedImage {
            anchors.fill: parent
            source: pane.source
        }

        Item {
            id: frame
            x: pane.crop.cx * pane.boxWidth - width / 2
            y: pane.crop.cy * pane.boxHeight - height / 2
            width: pane.crop.scale * pane.fullWidth
            height: pane.crop.scale * pane.fullHeight
            rotation: pane.crop.angle

            // Fora do quadro: escurecido.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 2 * 9999
                height: parent.height + 2 * 9999
                color: "transparent"
                border.width: 9999
                border.color: Qt.rgba(0, 0, 0, 0.55)
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 2
                border.color: Theme.brand400
            }

            // Regra dos terços
            Repeater {
                model: [1 / 3, 2 / 3]
                Rectangle {
                    required property real modelData
                    x: frame.width * modelData
                    width: 1
                    height: frame.height
                    color: Qt.rgba(1, 1, 1, 0.4)
                }
            }
            Repeater {
                model: [1 / 3, 2 / 3]
                Rectangle {
                    required property real modelData
                    y: frame.height * modelData
                    width: frame.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.4)
                }
            }

            // Cantos em "L": 16px de braço, 3px de traço, 2px para fora.
            Repeater {
                model: [[-1, -1], [1, -1], [-1, 1], [1, 1]]
                Item {
                    id: corner
                    required property var modelData
                    readonly property int sx: modelData[0]
                    readonly property int sy: modelData[1]
                    width: 16
                    height: 16
                    x: sx < 0 ? -2 : frame.width - 14
                    y: sy < 0 ? -2 : frame.height - 14

                    Rectangle {
                        x: 0
                        y: corner.sy < 0 ? 0 : 13
                        width: 16
                        height: 3
                        color: Theme.white
                    }
                    Rectangle {
                        x: corner.sx < 0 ? 0 : 13
                        y: 0
                        width: 3
                        height: 16
                        color: Theme.white
                    }
                }
            }
        }
    }

    MouseArea {
        id: mouse

        property var drag: null

        // O que está sob o ponteiro: [modo, sx, sy].
        function hit(px, py) {
            const p = mapToItem(frame, px, py);
            const w = frame.width;
            const h = frame.height;
            // Área de toque de 36px em volta de cada canto (12px para fora).
            for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
                const x0 = sx < 0 ? -12 : w - 24;
                const y0 = sy < 0 ? -12 : h - 24;
                if (p.x >= x0 && p.x <= x0 + 36 && p.y >= y0 && p.y <= y0 + 36)
                    return ["resize", sx, sy];
            }
            if (p.x >= 0 && p.x <= w && p.y >= 0 && p.y <= h)
                return ["move", 0, 0];
            return ["rotate", 0, 0];
        }

        function cursorFor(h) {
            if (h[0] === "resize")
                return h[1] * h[2] > 0 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor;
            if (h[0] === "move")
                return Qt.SizeAllCursor;
            return drag ? Qt.ClosedHandCursor : Qt.OpenHandCursor;
        }

        // Canto oposto FIXO (como no Lightroom): o ponteiro, levado para os
        // eixos do quadro girado, define a nova largura; a altura segue a proporção.
        function resize(d, x, y) {
            const c0 = d.crop;
            const t = c0.angle * Math.PI / 180;
            const cos = Math.cos(t);
            const sin = Math.sin(t);
            const hw = c0.scale * pane.fullWidth / 2;
            const hh = c0.scale * pane.fullHeight / 2;
            const cx = d.c[0];
            const cy = d.c[1];
            const ox = cx - cos * d.sx * hw + sin * d.sy * hh;
            const oy = cy - sin * d.sx * hw - cos * d.sy * hh;
            const dx = x - ox;
            const dy = y - oy;
            const u = cos * dx + sin * dy;
            const v = -sin * dx + cos * dy;
            const w = Math.max(d.sx * u, d.sy * v * (pane.fullWidth / pane.fullHeight), 1);
            const h = w * pane.fullHeight / pane.fullWidth;
            const ncx = ox + cos * (d.sx * w) / 2 - sin * (d.sy * h) / 2;
            const ncy = oy + sin * (d.sx * w) / 2 + cos * (d.sy * h) / 2;
            return Object.assign({}, c0, { scale: w / pane.fullWidth, cx: ncx / pane.boxWidth, cy: ncy / pane.boxHeight });
        }

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.OpenHandCursor

        onPressed: (event) => {
            if (pane.boxWidth <= 0)
                return;
            const h = hit(event.x, event.y);
            const p = mapToItem(box, event.x, event.y);
            drag = { mode: h[0], sx: h[1], sy: h[2], x: p.x, y: p.y,
                     crop: Object.assign({}, pane.crop),
                     c: [pane.crop.cx * pane.boxWidth, pane.crop.cy * pane.boxHeight] };
            cursorShape = cursorFor(h);
        }
        onPositionChanged: (event) => {
            const d = drag;
            if (!d) {
                cursorShape = cursorFor(hit(event.x, event.y));
                return;
            }
            const p = mapToItem(box, event.x, event.y);
            let next;
            if (d.mode === "move") {
                next = Object.assign({}, d.crop, { cx: d.crop.cx + (p.x - d.x) / pane.boxWidth,
                                                   cy: d.crop.cy + (p.y - d.y) / pane.boxHeight });
            } else if (d.mode === "resize") {
                next = resize(d, p.x, p.y);
            } else {
                const a0 = Math.atan2(d.y - d.c[1], d.x - d.c[0]);
                const a1 = Math.atan2(p.y - d.c[1], p.x - d.c[0]);
                next = Object.assign({}, d.crop, { angle: d.crop.angle + (a1 - a0) * 180 / Math.PI });
            }
            pane.edited(App.editor.normalizeCrop(next), d.mode);
        }
        function end() {
            if (!drag)
                return;
            drag = null;
            pane.committed();
        }
        onReleased: (event) => {
            end();
            cursorShape = cursorFor(hit(event.x, event.y));
        }
        onCanceled: end()
    }
}
