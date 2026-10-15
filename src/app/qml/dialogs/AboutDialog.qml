pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Ajuda > Sobre: versão e onde ficam os dados e os logs (o about.text do
// shell antigo), em Modal do app.
Modal {
    id: about

    title: qsTr("About %1").arg(App.brandName)
    size: "md"
    showIcon: false

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        Image {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 28
            Layout.preferredWidth: 28 * 2594 / 575
            source: Theme.dark ? "qrc:/qt/qml/PhotoEditor/assets/logo/photoe-dark.png"
                               : "qrc:/qt/qml/PhotoEditor/assets/logo/photoe-light.png"
            sourceSize.height: 28 * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
        }

        Text {
            Layout.fillWidth: true
            text: App.brandName + " " + App.version
            font.pixelSize: Theme.fontBase
            font.weight: Font.Medium
            color: Theme.foreground
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Event photo editor.")
            font.pixelSize: Theme.fontBase
            color: Theme.mutedForeground
            horizontalAlignment: Text.AlignHCenter
        }

        Repeater {
            model: [
                { label: qsTr("Data"), value: App.dataDir },
                { label: qsTr("Logs"), value: App.logsDir }
            ]
            ColumnLayout {
                id: entry
                required property var modelData
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: entry.modelData.label
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.Medium
                    color: Theme.mutedForeground
                    horizontalAlignment: Text.AlignHCenter
                }
                // Selecionável: dá para copiar o caminho.
                TextEdit {
                    Layout.fillWidth: true
                    text: entry.modelData.value
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.pixelSize: Theme.fontSm
                    color: Theme.foreground
                    selectionColor: Theme.alpha(Theme.brand400, 0.45)
                    horizontalAlignment: TextEdit.AlignHCenter
                }
            }
        }
    }

    footer: [
        Button {
            text: qsTr("Open Logs Folder")
            variant: "outline"
            iconName: "folder-open"
            onClicked: App.openLogsFolder()
        },
        Button {
            text: qsTr("Close")
            onClicked: about.close()
        }
    ]
}
