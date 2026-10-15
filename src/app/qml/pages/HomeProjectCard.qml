pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import PhotoEditor

// Card de um projeto recente (ProjectCard do Home.jsx): capa quadrada, selo
// "Aberto", botões que aparecem ao passar o mouse (mostrar na pasta,
// remover da lista), nome, "N fotos · tamanho" e "Aberto em <data>".
Item {
    id: card

    /// Item de App.recentProjects: {path, name, coverUrl, openedAt, exists,
    /// hasRaw, photoCount, sizeBytes, isCurrent}.
    property var project: ({})

    signal openRequested()
    signal removeRequested()
    signal revealRequested()

    readonly property bool hovered: mouse.containsMouse || revealButton.hovered || removeButton.hovered
    readonly property bool coverFailed: cover.status === Image.Error
    // Sem pasta: FolderX; sem fotos ou capa que não carregou: ImageOff.
    readonly property string placeholder: !project.exists ? "folder-x"
        : (!project.photoCount || !project.coverUrl || coverFailed) ? "image-off" : ""

    readonly property bool effects: GraphicsInfo.api !== GraphicsInfo.Software

    implicitWidth: 150
    implicitHeight: coverBox.height + info.implicitHeight + 8
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: project.name || ""
    Keys.onReturnPressed: openRequested()
    Keys.onSpacePressed: openRequested()

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.openRequested()
    }

    Tip {
        text: card.project.path || ""
        visible: mouse.containsMouse
        y: coverBox.height - 8
    }

    // Capa (aspect-square, rounded-xl, bg-muted, ring).
    Item {
        id: coverBox
        width: parent.width
        height: width
        opacity: card.project.exists ? 1 : 0.6

        Item {
            id: coverContent
            anchors.fill: parent
            // Cantos arredondados por máscara; sem GPU, recorte reto.
            clip: !card.effects
            layer.enabled: card.effects
            layer.smooth: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: coverMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.muted
            }

            Image {
                id: cover
                anchors.fill: parent
                visible: card.placeholder === ""
                source: card.project.exists && card.project.photoCount ? (card.project.coverUrl || "") : ""
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(480, 480)
                smooth: true
                scale: card.hovered ? 1.03 : 1
                Behavior on scale { NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: card.placeholder !== ""
                spacing: 8

                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: card.placeholder
                    size: 32
                    color: Theme.mutedForeground
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !card.project.exists
                    text: qsTr("Folder not found")
                    font.pixelSize: Theme.fontXs
                    color: Theme.mutedForeground
                }
            }
        }

        Rectangle {
            id: coverMask
            anchors.fill: parent
            radius: Theme.radiusXl
            visible: false
            layer.enabled: true
            layer.smooth: true
        }

        // ring-1 ring-border; hover/foco: ring-2 ring-brand-400.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusXl
            color: "transparent"
            border.width: card.hovered || card.activeFocus ? 2 : 1
            border.color: card.hovered || card.activeFocus ? Theme.brand400 : Theme.border
        }

        Badge {
            visible: card.project.isCurrent === true
            x: 8
            y: 8
            text: qsTr("Open")
        }

        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 4
            opacity: card.hovered || card.activeFocus ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.animationFast } }

            IconButton {
                id: revealButton
                visible: card.project.exists === true
                variant: "overlay"
                diameter: 28
                iconSize: 14
                iconName: "folder-search"
                tooltip: qsTr("Show in folder")
                onClicked: card.revealRequested()
            }
            IconButton {
                id: removeButton
                variant: "overlay"
                diameter: 28
                iconSize: 14
                iconName: "x"
                tooltip: qsTr("Remove from list")
                onClicked: card.removeRequested()
            }
        }
    }

    ColumnLayout {
        id: info
        anchors.top: coverBox.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        spacing: 0

        Text {
            Layout.fillWidth: true
            text: card.project.name || ""
            font.pixelSize: Theme.fontBase
            font.weight: Font.Medium
            color: Theme.foreground
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: card.project.exists
                  ? qsTr("%n photos", "", card.project.photoCount || 0) + " · " + App.formatBytes(card.project.sizeBytes || 0)
                  : (card.project.path || "")
            font.pixelSize: Theme.fontSm
            color: Theme.mutedForeground
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: {
                const d = card.project.openedAt;
                if (!d || isNaN(d.getTime()))
                    return "";
                return qsTr("Opened %1").arg(d.toLocaleDateString(Qt.locale(), "dd MMM yyyy"));
            }
            font.pixelSize: Theme.fontXs
            color: Theme.alpha(Theme.mutedForeground, 0.8)
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.fill: coverBox
        anchors.margins: -3
        radius: Theme.radiusXl + 3
        color: "transparent"
        border.width: 2
        border.color: Theme.focusRing
        visible: card.activeFocus
    }
}
