import QtQuick
import PhotoEditor

// Máscara pintada por cima da foto, na cor de acento (o MaskOverlay do
// SelectEditor.jsx). O provider devolve o overlay já tingido (?color=): a cor
// fica no tema, não no arquivo. Ocupa a caixa exata da foto (anchors.fill).
//
//   MaskOverlay { anchors.fill: photoBox; url: App.editor.activeLayerMaskUrl; faint: true }
BufferedImage {
    id: overlay

    /// image://photo/mask/<chave> ('' = sem máscara)
    property string url: ""
    /// Mais apagada (a camada ativa fora do modo seleção).
    property bool faint: false

    visible: url !== ""
    opacity: faint ? 0.35 : 0.55
    source: url !== "" ? url + "?color=" + encodeURIComponent(Theme.hex(Theme.brand500)) : ""
}
