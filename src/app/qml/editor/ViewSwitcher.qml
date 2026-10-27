pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// O que a filmstrip mostra (App.editor.view): Fotos | Instagram | Arquivadas | Lixeira.
// Controle segmentado compacto: uma moldura só, o segmento ativo no acento
// da marca; a contagem de arquivadas e da lixeira vai ao lado do rótulo e
// some quando é zero. ``compact`` (barra estreita) deixa só ícone e
// contagem; o nome vai para a dica.
Rectangle {
    id: switcher

    property bool compact: false

    readonly property var options: [
        { value: "photos", text: qsTr("Photos"), icon: "image", count: -1 },
        { value: "instagram", text: qsTr("Instagram"), icon: "instagram", count: App.editor.instagramCount },
        { value: "archived", text: qsTr("Archived"), icon: "archive", count: App.editor.archivedCount },
        { value: "trash", text: qsTr("Trash"), icon: "trash-2", count: App.editor.trashCount }
    ]

    implicitWidth: segments.implicitWidth + 4
    implicitHeight: 32
    radius: Theme.radiusMd
    color: "transparent"
    border.color: Theme.border

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Filmstrip view")

    Row {
        id: segments
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: switcher.options

            T.AbstractButton {
                id: segment

                required property var modelData
                readonly property bool active: App.editor.view === modelData.value

                implicitWidth: content.implicitWidth + 20
                implicitHeight: switcher.height - 4
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.count > 0
                    ? qsTr("%1 (%2)").arg(modelData.text).arg(modelData.count) : modelData.text
                Accessible.checkable: true
                Accessible.checked: active

                onClicked: App.editor.view = modelData.value

                Tip {
                    text: segment.modelData.text
                    visible: switcher.compact && segment.hovered
                }

                contentItem: Item {
                    implicitWidth: content.implicitWidth
                    implicitHeight: content.implicitHeight

                    Row {
                        id: content
                        anchors.centerIn: parent
                        spacing: 6

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: segment.modelData.icon
                            size: 14
                            color: label.color
                        }
                        Text {
                            id: label
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !switcher.compact
                            text: segment.modelData.text
                            font.pixelSize: Theme.fontSm
                            font.weight: segment.active ? Font.DemiBold : Font.Medium
                            color: segment.active ? Theme.brandForeground
                                 : segment.hovered ? Theme.foreground : Theme.mutedForeground
                        }
                        // Contagem (some em zero; nas Fotos não há)
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: segment.modelData.count > 0
                            implicitWidth: Math.max(18, countLabel.implicitWidth + 10)
                            implicitHeight: 16
                            radius: 8
                            color: segment.active ? Theme.alpha(Theme.brand400, 0.2) : Theme.muted
                            Text {
                                id: countLabel
                                anchors.centerIn: parent
                                text: segment.modelData.count
                                font.pixelSize: Theme.fontTiny
                                font.weight: Font.DemiBold
                                color: segment.active ? Theme.brandForeground : Theme.mutedForeground
                            }
                        }
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
