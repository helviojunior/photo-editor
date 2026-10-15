import QtQuick
import PhotoEditor

// Rótulo no canto de um painel de foto ("ORIGINAL", "EDITADA", "RESULTADO"):
// o figcaption das telas React — preto/60, 11px, maiúsculas espaçadas.
Rectangle {
    id: caption

    property string text: ""
    /// Largura máxima (o rótulo trunca com reticências).
    property real maximumWidth: 400

    x: 8
    y: 8
    z: 20
    visible: text !== ""
    width: Math.min(label.implicitWidth, maximumWidth - 16) + 16
    height: label.implicitHeight + 4
    radius: Theme.radiusSm
    color: Theme.imageLabel

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, caption.maximumWidth - 16)
        text: caption.text
        font.pixelSize: Theme.fontXs
        font.weight: Font.Medium
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.5
        color: Qt.rgba(1, 1, 1, 0.9)
        elide: Text.ElideRight
    }
}
