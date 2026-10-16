import QtQuick
import PhotoEditor

// Triângulo de arrastar dos Níveis (preto, gama, branco; saída preta e
// branca), apontando para cima, com o centro da base em ``position`` (px).
// Só desenha: quem arrasta é o MouseArea da faixa (LevelsEditor), que escolhe
// o triângulo mais perto do clique.
Canvas {
    id: handle

    property real position: 0
    property color fill: Theme.white
    property color stroke: Theme.mutedForeground
    property bool active: false

    x: Math.round(position - width / 2)
    width: 12
    height: 11
    onFillChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onActiveChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        ctx.beginPath();
        ctx.moveTo(width / 2, 0.5);
        ctx.lineTo(width - 0.5, height - 0.5);
        ctx.lineTo(0.5, height - 0.5);
        ctx.closePath();
        ctx.fillStyle = Theme.css(fill, -1);
        ctx.fill();
        ctx.lineWidth = active ? 2 : 1;
        ctx.strokeStyle = Theme.css(active ? Theme.focusRing : stroke, -1);
        ctx.stroke();
    }
}
