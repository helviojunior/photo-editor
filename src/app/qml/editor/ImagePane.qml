import QtQuick
import PhotoEditor

// Um lado do antes/depois (ImagePane.jsx): rótulo no topo e a foto inteira
// visível (PreserveAspectFit) sobre fundo neutro, como no Develop do
// Lightroom. A imagem só troca quando a nova terminou de carregar e, enquanto
// isso, a anterior fica a 80%.
Rectangle {
    id: pane

    property string label: ""
    property url source

    readonly property bool busy: image.loading

    color: Theme.imagePane
    clip: true

    PaneCaption {
        text: pane.label
        maximumWidth: pane.width - 16
    }

    BufferedImage {
        id: image
        anchors.fill: parent
        anchors.margins: 8
        fillMode: Image.PreserveAspectFit
        source: pane.source
        opacity: pane.busy ? 0.8 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.animationFast } }
    }
}
