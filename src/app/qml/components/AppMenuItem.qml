import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import PhotoEditor

// Item do AppMenu (menu dentro da janela): texto, atalho à direita e seta de
// submenu. Sem efeito no menu nativo do macOS.
QQC.MenuItem {
    id: item

    implicitHeight: 30
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.fontBase

    // Texto do atalho no formato do SO ("Ctrl+O", "⌘O"). O Shortcut fica
    // desligado: só traduz a sequência, quem dispara é a Action.
    Shortcut {
        id: keys
        enabled: false
        sequence: item.action && item.action.shortcut ? item.action.shortcut : ""
    }

    contentItem: RowLayout {
        spacing: 16

        Text {
            Layout.fillWidth: true
            text: item.text.replace("&", "")
            font: item.font
            color: Theme.popoverForeground
            opacity: item.enabled ? 1 : 0.45
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            visible: text !== ""
            text: keys.nativeText
            font.pixelSize: Theme.fontSm
            color: Theme.mutedForeground
            opacity: item.enabled ? 1 : 0.45
        }
        Icon {
            visible: item.subMenu !== null
            name: "chevron-right"
            size: 14
            color: Theme.mutedForeground
        }
    }

    arrow: null
    indicator: null

    background: Rectangle {
        implicitWidth: 220
        radius: Theme.radiusSm
        color: item.highlighted && item.enabled ? Theme.accent : "transparent"
    }
}
