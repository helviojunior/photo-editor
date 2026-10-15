import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Botão só de ícone da barra da filmstrip (o IconButton do Editor.jsx: um
// Button ghost sm quadrado). ``active`` acende no acento da marca; ``filled``
// preenche o desenho (a estrela da capa). A dica é o nome acessível.
T.AbstractButton {
    id: control

    property string iconName: ""
    property string tooltip: ""
    property bool active: false
    property bool filled: false
    property bool loading: false

    readonly property color iconColor: active ? Theme.brand400 : Theme.foreground

    implicitWidth: Theme.controlHeightSm
    implicitHeight: Theme.controlHeightSm
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : 0.5
    Accessible.name: tooltip
    Accessible.checkable: active || filled
    Accessible.checked: active

    contentItem: Item {
        Spinner {
            anchors.centerIn: parent
            visible: control.loading
            color: control.iconColor
        }
        Image {
            anchors.centerIn: parent
            visible: !control.loading
            width: 16
            height: 16
            sourceSize: Qt.size(Math.ceil(16 * Screen.devicePixelRatio), Math.ceil(16 * Screen.devicePixelRatio))
            source: control.iconName
                ? Theme.iconUrl(control.iconName, control.iconColor) + (control.filled ? "&fill=1" : "")
                : ""
        }
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: (control.hovered || control.down) && control.enabled ? Theme.accent : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.ring
            visible: control.visualFocus
        }
    }

    Tip {
        text: control.tooltip
        visible: control.hovered && control.tooltip !== ""
    }
}
