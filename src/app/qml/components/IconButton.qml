import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Botão só com ícone, redondo, com dica (os botões do cabeçalho e das
// listas). A dica (``tooltip``) também é o nome acessível.
//
//   IconButton { iconName: "house"; tooltip: qsTr("Home"); onClicked: ... }
//
// variant: "ghost"   (padrão) ícone apagado, acende ao passar o mouse
//          "overlay" círculo preto translúcido sobre foto (cards da Home)
// active:  marca o botão da tela atual (ícone no acento da marca).
T.AbstractButton {
    id: control

    property string iconName: ""
    property string tooltip: ""
    property string variant: "ghost"
    property bool active: false
    /// Diâmetro do botão.
    property int diameter: 40
    property int iconSize: 18
    property color iconColor: {
        if (variant === "overlay")
            return Theme.white;
        if (active)
            return Theme.brand400;
        return hovered ? Theme.foreground : Theme.mutedForeground;
    }

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: diameter
    implicitHeight: diameter
    padding: 0
    opacity: enabled ? 1 : 0.5

    Accessible.name: tooltip

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.iconColor
        }
    }

    background: Rectangle {
        radius: width / 2
        color: {
            if (control.variant === "overlay")
                return Theme.alpha(Theme.black, control.hovered ? 0.8 : 0.6);
            return control.hovered || control.down ? Theme.muted : "transparent";
        }
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }

    Tip {
        text: control.tooltip
        visible: control.hovered && control.tooltip !== ""
    }
}
