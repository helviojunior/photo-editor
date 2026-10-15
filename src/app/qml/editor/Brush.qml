import QtQuick

// Estado do pincel da seleção (o brush do useBrushSelection.js): tamanho em
// fração do lado maior da foto, somar/subtrair e a detecção do objeto (SAM).
//
//   Brush { id: brush; size: 0.015 }      // o merge nasce com um pincel menor
//   brush.step(1)                         // ] — um passo maior
QtObject {
    property real size: 0.04
    /// "add" | "subtract"
    property string mode: "add"
    property bool smart: true

    readonly property real minimum: 0.005
    readonly property real maximum: 0.15
    readonly property real stepSize: 0.005

    /// [ e ]: um passo menor ou maior, dentro dos limites do slider.
    function step(direction) {
        size = Math.round(Math.min(Math.max(size + direction * stepSize, minimum), maximum) * 1000) / 1000;
    }
}
