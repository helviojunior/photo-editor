pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Progresso e resumo da exportação (TODO 7.3; components/editor/ExportDialog.jsx),
// no Modal do app (regra 1). Acompanha App.editor.exportStatus.
//
//   ExportDialog { id: exportDialog }
//   exportDialog.start()     // App.editor.startExport() + open()
//   exportDialog.open()      // só mostra o andamento/resumo da última
//
// Fechar durante a exportação não a cancela: ela segue no C++ e o botão
// Exportar do editor mostra o progresso até reabrir.
Modal {
    id: dialog

    readonly property var status: App.editor.exportStatus || ({})
    readonly property bool running: !!status.running
    readonly property int total: status.total || 0
    readonly property int done: status.done || 0
    readonly property int percent: total ? Math.round(done / total * 100) : 0
    readonly property var allErrors: status.errors || []
    // "*" = falha geral (sem nome de arquivo): vira a mensagem genérica.
    readonly property var namedErrors: allErrors.filter((e) => e !== "*")
    readonly property string outputDir: (status.outputDir || "publicar") + "/"

    /// Começa a exportação e abre o modal.
    function start() {
        App.editor.startExport();
        open();
    }

    size: "md"
    variant: running ? "brand" : "success"
    iconName: running ? "upload" : "circle-check"
    title: running ? qsTr("Exporting…") : qsTr("Export finished")
    description: qsTr("Active photos, with their adjustments, go to %1").arg(Messages.escapeHtml(outputDir))

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        // Barra de progresso (n de total + %).
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1 of %2").arg(dialog.done).arg(dialog.total)
                    font.pixelSize: Theme.fontSm
                    color: Theme.mutedForeground
                }
                Text {
                    text: dialog.percent + "%"
                    font.pixelSize: Theme.fontSm
                    font.features: { "tnum": 1 }
                    color: Theme.mutedForeground
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 8
                radius: 4
                color: Theme.muted
                clip: true

                Accessible.role: Accessible.ProgressBar
                Accessible.name: qsTr("%1 of %2").arg(dialog.done).arg(dialog.total)

                Rectangle {
                    height: parent.height
                    width: parent.width * dialog.percent / 100
                    radius: 4
                    color: Theme.brand500
                    Behavior on width { NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
                }
            }
        }

        // Resumo: exportadas / sem mudança / removidas.
        GridLayout {
            Layout.fillWidth: true
            visible: !dialog.running
            columns: 3
            columnSpacing: 8

            Repeater {
                model: [
                    { value: dialog.status.written || 0, label: qsTr("exported") },
                    { value: dialog.status.skipped || 0, label: qsTr("unchanged") },
                    { value: dialog.status.removed || 0, label: qsTr("removed") }
                ]
                Rectangle {
                    id: stat
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: statColumn.implicitHeight + 16
                    radius: Theme.radiusLg
                    color: "transparent"
                    border.color: Theme.border

                    ColumnLayout {
                        id: statColumn
                        anchors.centerIn: parent
                        width: parent.width - 16
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: stat.modelData.value
                            font.pixelSize: Theme.fontLg
                            font.weight: Font.DemiBold
                            font.features: { "tnum": 1 }
                            color: Theme.foreground
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            Layout.fillWidth: true
                            text: stat.modelData.label
                            font.pixelSize: Theme.fontXs
                            color: Theme.mutedForeground
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !dialog.running && (dialog.status.instagram || 0) > 0
            text: qsTr("%n Instagram version(s) in %1.", "", dialog.status.instagram || 0)
                .arg((dialog.status.instagramDir || "publicar/instagram") + "/")
            font.pixelSize: Theme.fontSm
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            visible: !dialog.running && !!dialog.status.cover
            text: qsTr("Cover saved as %1.").arg(dialog.outputDir + (dialog.status.cover || ""))
            font.pixelSize: Theme.fontSm
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }

        FormError {
            Layout.fillWidth: true
            items: dialog.allErrors.length > 0
                   ? [qsTr("Some photos could not be exported:")].concat(
                         dialog.namedErrors.length ? dialog.namedErrors : [Messages.error("error.generic")])
                   : []
        }
    }

    footer: [
        Button {
            variant: dialog.running ? "outline" : "default"
            text: dialog.running ? qsTr("Keep running in background") : qsTr("Close")
            onClicked: dialog.close()
        }
    ]
}
