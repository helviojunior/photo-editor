import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Botãozinho de ícone das linhas de lista (o LayerButton do LayersPanel.jsx):
// 24px, ícone de 14, apagado até passar o mouse. A dica é o nome acessível.
T.AbstractButton {
    id: control

    property string iconName: ""
    property string tooltip: ""

    implicitWidth: 24
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tooltip

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: 14
            color: control.hovered ? Theme.foreground : Theme.mutedForeground
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.hovered ? Theme.alpha(Theme.background, 0.6) : "transparent"
    }

    Tip {
        text: control.tooltip
        visible: control.hovered && control.tooltip !== ""
    }
}
