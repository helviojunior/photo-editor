pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Histórico completo da foto atual (PhotoHistory.jsx), o mais recente
// primeiro. O que já foi desfeito continua na lista, riscado: o banco guarda tudo.
ColumnLayout {
    id: section

    readonly property var entries: App.editor.history
    readonly property var locale: Qt.locale(String(App.language).replace("-", "_"))

    spacing: 4

    SectionTitle {
        Layout.bottomMargin: 4
        text: qsTr("History")
    }

    Text {
        visible: section.entries.length === 0
        text: qsTr("No actions yet.")
        font.pixelSize: Theme.fontSm
        color: Theme.mutedForeground
    }

    Repeater {
        model: section.entries

        RowLayout {
            id: entry
            required property var modelData
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: Messages.actionName(entry.modelData.kind)
                font.pixelSize: Theme.fontSm
                font.strikeout: entry.modelData.undone
                color: entry.modelData.undone ? Theme.mutedForeground : Theme.foreground
                elide: Text.ElideRight
            }
            Text {
                text: entry.modelData.created ? entry.modelData.created.toLocaleString(section.locale, Locale.ShortFormat) : ""
                font.pixelSize: Theme.fontXs
                color: Theme.mutedForeground
            }
        }
    }
}
