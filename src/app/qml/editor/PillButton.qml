import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Escolha em pílula dos painéis (presets): borda, texto 12px; a ativa fica no
// acento da marca (borda brand-400, fundo brand-400/15).
T.AbstractButton {
    id: control

    property bool active: false
    property real cornerRadius: height / 2

    implicitWidth: implicitContentWidth + 24
    implicitHeight: 30
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.checkable: true
    Accessible.checked: active

    contentItem: Text {
        text: control.text
        font.pixelSize: Theme.fontSm
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.active ? Theme.brand400 : control.hovered ? Theme.foreground : Theme.mutedForeground
    }

    background: Rectangle {
        radius: control.cornerRadius
        color: control.active ? Theme.alpha(Theme.brand400, 0.15) : control.hovered ? Theme.muted : "transparent"
        border.width: 1
        border.color: control.active ? Theme.brand400 : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }
}
