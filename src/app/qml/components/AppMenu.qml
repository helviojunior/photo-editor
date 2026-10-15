import QtQuick
import QtQuick.Controls as QQC
import PhotoEditor

// Menu no tema do app. No macOS, dentro da MenuBar, o Qt 6.8 usa o menu
// NATIVO do sistema e ignora delegate/background; no Windows/Linux (menu
// dentro da janela) vale este desenho.
//
//   AppMenu {
//       title: qsTr("&File")
//       QQC.Action { text: qsTr("Open…"); shortcut: StandardKey.Open; onTriggered: ... }
//       QQC.MenuSeparator {}
//   }
QQC.Menu {
    id: menu

    topPadding: 4
    bottomPadding: 4
    leftPadding: 4
    rightPadding: 4
    font.pixelSize: Theme.fontBase

    delegate: AppMenuItem {}

    background: Rectangle {
        implicitWidth: 240
        color: Theme.popover
        border.color: Theme.border
        radius: Theme.radiusMd
    }
}
