pragma ComponentBehavior: Bound
import QtQuick
import PhotoEditor

// Resultado do merge (CompositePane do MergePanel.jsx): o preview da base e,
// empilhados por cima, os PNGs das áreas já alinhadas — primeiro o fundo por
// trás do objeto da base (opacidade 1 − a do objeto), depois as camadas em
// ordem. Todos têm o tamanho do preview da base e ocupam a mesma caixa, então
// cada pixel cai no lugar certo; a transparência é a ``opacity`` de cada um.
//
//   pieces: [{key, url, opacity}]
Rectangle {
    id: pane

    property string label: ""
    property url baseUrl
    /// Altura/largura da base.
    property real aspect: 2 / 3
    property var pieces: []
    property bool busy: false

    readonly property real boxWidth: Math.max(0, Math.min(width - 16, (height - 16) / (aspect > 0 ? aspect : 1)))
    readonly property real boxHeight: boxWidth * (aspect > 0 ? aspect : 1)

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
        anchors.centerIn: parent
        width: pane.boxWidth
        height: pane.boxHeight

        BufferedImage {
            anchors.fill: parent
            source: pane.baseUrl
        }

        Repeater {
            model: pane.pieces

            BufferedImage {
                required property var modelData
                anchors.fill: parent
                source: modelData.url
                opacity: modelData.opacity
            }
        }
    }
}
