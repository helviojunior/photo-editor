pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Controle segmentado que ocupa a largura toda (o ViewSwitcher, esticado):
// as abas do painel de edição e os canais RGB/R/G/B de Níveis e Curvas.
//
//   Segmented {
//       Layout.fillWidth: true
//       value: panel.tab
//       options: [{ value: "general", text: qsTr("General"), tip: "..." }, ...]
//       onActivated: (v) => panel.tab = v
//   }
//
// Cada opção: {value, text, tip?, accent? (cor do texto ativo, ex. o do
// canal), marked? (bolinha: "este canal tem ajuste")}.
Rectangle {
    id: control

    property var options: []
    property string value: ""
    property string accessibleName: ""
    property int segmentHeight: 28

    signal activated(string value)

    implicitWidth: 240
    implicitHeight: segmentHeight + 4
    radius: Theme.radiusMd
    color: "transparent"
    border.color: Theme.border

    Accessible.role: Accessible.PageTabList
    Accessible.name: accessibleName

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2

        Repeater {
            model: control.options

            T.AbstractButton {
                id: segment

                required property var modelData
                required property int index
                readonly property bool active: control.value === modelData.value
                readonly property color activeColor: modelData.accent !== undefined ? modelData.accent : Theme.brandForeground

                width: (row.width - row.spacing * (control.options.length - 1)) / Math.max(1, control.options.length)
                height: row.height
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.tip || modelData.text
                Accessible.checkable: true
                Accessible.checked: active

                onClicked: control.activated(modelData.value)

                Tip {
                    text: segment.modelData.tip || ""
                    visible: segment.hovered && text !== ""
                }

                contentItem: Item {
                    Text {
                        id: label
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width - 6)
                        text: segment.modelData.text
                        font.pixelSize: Theme.fontSm
                        font.weight: segment.active ? Font.DemiBold : Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        color: segment.active ? segment.activeColor
                             : segment.hovered ? Theme.foreground : Theme.mutedForeground
                    }
                    Rectangle {
                        visible: segment.modelData.marked === true
                        anchors.left: label.right
                        anchors.leftMargin: 3
                        anchors.top: label.top
                        width: 5
                        height: 5
                        radius: 2.5
                        color: segment.active ? segment.activeColor : Theme.mutedForeground
                    }
                }

                background: Rectangle {
                    radius: Theme.radiusSm
                    color: segment.active ? Theme.brandSoft : segment.hovered ? Theme.muted : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animationFast } }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: parent.radius + 3
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.focusRing
                        visible: segment.visualFocus
                    }
                }
            }
        }
    }
}
