import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Uma camada do painel do merge (LayerRow do MergePanel.jsx): cabeçalho
// clicável (thumbnail, nome, estado da área), alinhamento, Selecionar/Editar
// área, Visível, remover e a opacidade. Na base: "Opacidade do objeto",
// desabilitada até a base ter área.
Rectangle {
    id: row

    /// Item de App.merge.layers (ou a base, montada no MergePanel).
    property var entry: ({})
    property bool isBase: false

    signal selectRequested()
    signal removeRequested()

    readonly property bool active: entry.active === true
    readonly property var align: entry.align
    readonly property bool missing: !entry.hasMask && !isBase

    function fmt(value, digits) {
        return Number(value || 0).toLocaleString(Qt.locale(String(App.language).replace("-", "_")), "f", digits);
    }

    implicitHeight: column.implicitHeight
    radius: Theme.radiusMd
    color: "transparent"
    border.width: 1
    border.color: active ? Theme.brand400 : Theme.border

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        // Cabeçalho: escolhe a camada ativa.
        Rectangle {
            id: header
            Layout.fillWidth: true
            Layout.margins: 1
            implicitHeight: headerRow.implicitHeight + 16
            topLeftRadius: Theme.radiusMd - 1
            topRightRadius: Theme.radiusMd - 1
            color: row.active ? Theme.alpha(Theme.brand400, 0.15) : headerMouse.containsMouse ? Theme.muted : "transparent"
            activeFocusOnTab: true

            Accessible.role: Accessible.RadioButton
            Accessible.name: row.entry.fileName || ""
            Accessible.checkable: true
            Accessible.checked: row.active
            Keys.onReturnPressed: App.merge.activePhoto = row.entry.photoId
            Keys.onSpacePressed: App.merge.activePhoto = row.entry.photoId

            MouseArea {
                id: headerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: App.merge.activePhoto = row.entry.photoId
            }

            RowLayout {
                id: headerRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Rectangle {
                    implicitWidth: 56
                    implicitHeight: 40
                    radius: 2
                    color: Theme.neutral800
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: row.entry.thumbUrl || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: row.entry.fileName || ""
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.Medium
                        color: Theme.foreground
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.isBase
                            ? (row.entry.hasMask ? qsTr("Base · object selected") : qsTr("Base · select its object to fade it"))
                            : (row.entry.hasMask ? qsTr("Area selected") : qsTr("No area selected"))
                        font.pixelSize: Theme.fontXs
                        color: row.missing ? Theme.amber400 : Theme.mutedForeground
                        elide: Text.ElideRight
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: header.activeFocus
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            spacing: 8

            FormError {
                Layout.fillWidth: true
                visible: !row.isBase && row.entry.available === false
                text: qsTr("This photo is no longer in raw/; the layer is skipped.")
            }
            Text {
                id: alignText
                Layout.fillWidth: true
                visible: !row.isBase && row.entry.available !== false && row.align !== null && row.align !== undefined && row.align.ok === true
                text: row.align
                    ? qsTr("Aligned: %1, %2 px · %3° · %4%").arg(row.fmt(row.align.dx, 1)).arg(row.fmt(row.align.dy, 1))
                        .arg(row.fmt(row.align.angle, 2)).arg(row.fmt(row.align.scale * 100, 2))
                    : ""
                font.pixelSize: Theme.fontXs
                font.features: { "tnum": 1 }
                color: Theme.mutedForeground
                elide: Text.ElideRight

                HoverHandler { id: alignHover }
                Tip {
                    text: row.align
                        ? qsTr("%1 background points, median error %2 px").arg(row.align.inliers || 0)
                            .arg(row.fmt(row.align.error === undefined || row.align.error === null ? 0 : row.align.error, 2))
                        : ""
                    visible: alignHover.hovered
                }
            }
            FormError {
                Layout.fillWidth: true
                visible: !row.isBase && row.entry.available !== false && !(row.align && row.align.ok === true)
                text: qsTr("Could not align this photo to the base; it is placed as is.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    Layout.fillWidth: true
                    size: "sm"
                    variant: row.entry.hasMask ? "outline" : "default"
                    iconName: row.entry.hasMask ? "pencil" : "brush"
                    text: row.entry.hasMask ? qsTr("Edit area") : qsTr("Select area")
                    disabled: row.entry.available === false
                    onClicked: row.selectRequested()
                }
                Toggle {
                    visible: !row.isBase
                    label: qsTr("Visible")
                    checked: row.entry.visible === true
                    onToggled: (value) => App.merge.setVisible(row.entry.photoId, value)
                }
                ToolButton {
                    visible: !row.isBase
                    iconName: "trash-2"
                    tooltip: qsTr("Remove from the merge")
                    onClicked: row.removeRequested()
                }
            }

            ValueSlider {
                Layout.fillWidth: true
                label: row.isBase ? qsTr("Object opacity") : qsTr("Opacity")
                from: 0
                to: 1
                stepSize: 0.01
                value: row.entry.opacity || 0
                defaultValue: 1
                showSign: false
                resetOnDoubleClick: false
                valueText: Math.round(value * 100) + "%"
                disabled: row.isBase && !row.entry.hasMask
                onMoved: (v) => App.merge.setOpacity(row.entry.photoId, v)
            }
        }
    }
}
