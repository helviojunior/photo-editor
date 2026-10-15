pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import PhotoEditor

// Tela inicial (Home.jsx): abrir/criar projeto e os projetos recentes.
//
// Um "projeto" é a pasta de um evento (raw/ + project_data/ ...). A lista vem
// do banco do app (~/.photoe/photoe.db) via App.recentProjects; abrir, criar
// e remover passam pelos ProjectFlows (diálogos do SO + modais do app).
ScrollPage {
    id: page

    /// ProjectFlows do Main.
    required property var flows

    property string query: ""

    readonly property var projects: App.recentProjects
    readonly property var visibleProjects: {
        const q = query.trim().toLowerCase();
        if (!q)
            return projects;
        return projects.filter((p) => p.name.toLowerCase().includes(q) || p.path.toLowerCase().includes(q));
    }

    spacing: 32

    Component.onCompleted: App.reloadRecent()

    // ------------------------------------------------------------------ //
    // Abrir / Novo
    // ------------------------------------------------------------------ //
    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 16
        rowSpacing: 16

        // "Abrir projeto": o cartão grande, em degradê da marca.
        Item {
            id: openCard
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            Layout.preferredHeight: page.width >= 976 ? 168 : 132
            activeFocusOnTab: true

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Open project")
            Keys.onReturnPressed: page.flows.openDialog()
            Keys.onSpacePressed: page.flows.openDialog()

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                // bg-gradient-to-br from-brand-600 via-brand-800 to-zinc-950 + ring-white/10
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.1)
                    strokeWidth: 1
                    fillGradient: LinearGradient {
                        x1: 0
                        y1: 0
                        x2: openCard.width
                        y2: openCard.height
                        GradientStop { position: 0; color: Theme.brand600 }
                        GradientStop { position: 0.5; color: Theme.brand800 }
                        GradientStop { position: 1; color: Theme.zinc950 }
                    }
                    PathRectangle {
                        x: 0.5
                        y: 0.5
                        width: openCard.width - 1
                        height: openCard.height - 1
                        radius: Theme.radius2xl
                    }
                }
            }

            // Brilhos radiais (o <span> com radial-gradient), mais fortes no hover.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                opacity: openMouse.containsMouse ? 1 : 0.7
                Behavior on opacity { NumberAnimation { duration: Theme.animationNormal } }

                ShapePath {
                    strokeWidth: -1
                    fillGradient: RadialGradient {
                        centerX: openCard.width * 0.3
                        centerY: openCard.height * 1.2
                        centerRadius: openCard.width * 0.6
                        focalX: centerX
                        focalY: centerY
                        GradientStop { position: 0; color: Qt.rgba(248 / 255, 94 / 255, 98 / 255, 0.55) }
                        GradientStop { position: 1; color: "transparent" }
                    }
                    PathRectangle {
                        width: openCard.width
                        height: openCard.height
                        radius: Theme.radius2xl
                    }
                }
                ShapePath {
                    strokeWidth: -1
                    fillGradient: RadialGradient {
                        centerX: openCard.width * 0.85
                        centerY: -openCard.height * 0.2
                        centerRadius: openCard.width * 0.5
                        focalX: centerX
                        focalY: centerY
                        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.18) }
                        GradientStop { position: 1; color: "transparent" }
                    }
                    PathRectangle {
                        width: openCard.width
                        height: openCard.height
                        radius: Theme.radius2xl
                    }
                }
            }

            Row {
                anchors.centerIn: parent
                spacing: 12

                Rectangle {
                    width: 40
                    height: 40
                    radius: Theme.radiusLg
                    color: Qt.rgba(1, 1, 1, 0.15)
                    anchors.verticalCenter: parent.verticalCenter

                    Icon {
                        anchors.centerIn: parent
                        name: "folder-open"
                        size: 20
                        color: Theme.white
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: qsTr("Open project")
                        font.pixelSize: Theme.fontLg
                        font.weight: Font.DemiBold
                        color: Theme.white
                    }
                    Text {
                        text: qsTr("Choose the event folder (the one with raw/ inside)")
                        font.pixelSize: Theme.fontSm
                        color: Qt.rgba(1, 1, 1, 0.7)
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: Theme.radius2xl + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.brand300
                visible: openCard.activeFocus
            }

            MouseArea {
                id: openMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.flows.openDialog()
            }
        }

        // "Novo projeto".
        Rectangle {
            id: newCard
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            radius: Theme.radius2xl
            color: newMouse.containsMouse ? Theme.muted : Theme.card
            border.color: Theme.border
            activeFocusOnTab: true
            Behavior on color { ColorAnimation { duration: Theme.animationFast } }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("New project")
            Keys.onReturnPressed: page.flows.newProject()
            Keys.onSpacePressed: page.flows.newProject()

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 16
                anchors.bottomMargin: 16
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: Theme.radiusLg
                    color: Theme.alpha(Theme.brand500, 0.15)

                    Icon {
                        anchors.centerIn: parent
                        name: "folder-plus"
                        size: 20
                        color: Theme.brand400
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("New project")
                        font.pixelSize: Theme.fontBase
                        font.weight: Font.DemiBold
                        color: Theme.foreground
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Pick the event folder — its JPEGs are moved into raw/")
                        font.pixelSize: Theme.fontSm
                        color: Theme.mutedForeground
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: Theme.radius2xl + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: newCard.activeFocus
            }

            MouseArea {
                id: newMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.flows.newProject()
            }
        }
    }

    // ------------------------------------------------------------------ //
    // Projetos recentes
    // ------------------------------------------------------------------ //
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 36
            spacing: 12

            Text {
                Layout.fillWidth: true
                textFormat: Text.StyledText
                text: qsTr("Projects") + " <font color=\"" + Theme.hex(Theme.mutedForeground) + "\">("
                      + page.projects.length + ")</font>"
                font.pixelSize: Theme.fontBase
                font.weight: Font.DemiBold
                color: Theme.foreground
            }

            TextField {
                visible: page.projects.length > 0
                Layout.preferredWidth: 224
                implicitHeight: Theme.controlHeightSm
                iconName: "search"
                placeholderText: qsTr("Search projects")
                Accessible.name: qsTr("Search projects")
                text: page.query
                onTextEdited: page.query = text
            }
        }

        // Nenhum projeto ainda.
        Item {
            id: emptyBox
            Layout.fillWidth: true
            visible: page.projects.length === 0
            implicitHeight: emptyColumn.implicitHeight + 96

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.border
                    strokeWidth: 1
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [4, 3]
                    fillColor: "transparent"
                    PathRectangle {
                        x: 0.5
                        y: 0.5
                        width: emptyBox.width - 1
                        height: emptyBox.height - 1
                        radius: Theme.radius2xl
                    }
                }
            }

            ColumnLayout {
                id: emptyColumn
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: qsTr("No projects yet")
                    font.pixelSize: Theme.fontBase
                    font.weight: Font.Medium
                    color: Theme.foreground
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Open an event folder to start — it will be listed here next time.")
                    font.pixelSize: Theme.fontSm
                    color: Theme.mutedForeground
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }

        // grid-cols-[repeat(auto-fill,minmax(150px,1fr))] gap-x-4 gap-y-6
        Grid {
            id: grid

            readonly property int minCell: 150
            readonly property int gap: 16
            readonly property int cols: Math.max(1, Math.floor((width + gap) / (minCell + gap)))
            readonly property real cellWidth: (width - (cols - 1) * gap) / cols

            Layout.fillWidth: true
            columns: cols
            columnSpacing: gap
            rowSpacing: 24

            Repeater {
                model: page.visibleProjects

                HomeProjectCard {
                    id: projectCard
                    required property var modelData
                    width: grid.cellWidth
                    project: projectCard.modelData
                    onOpenRequested: page.flows.openRecent(projectCard.modelData)
                    onRemoveRequested: page.flows.removeRecent(projectCard.modelData)
                    onRevealRequested: App.revealInFolder(projectCard.modelData.path)
                }
            }
        }
    }
}
