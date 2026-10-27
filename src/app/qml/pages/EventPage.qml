pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Dados do evento (nome de exibição, campeonato e categoria). O nome vazio
// segue a pasta do projeto; estes campos também alimentam a legenda do
// Instagram ({event}, {championship}, {category}).
//
// Detalhe de entidade é uma página (regra 3), não um modal. Guardado no
// db.sqlite3 do projeto (App.saveEvent); a pasta não é renomeada.
ScrollPage {
    id: page

    /// Volta para a tela de onde veio (Main).
    signal backRequested()

    property bool saved: false

    readonly property bool dirty: nameField.text !== App.eventName
        || championshipField.text !== App.eventChampionship
        || categoryField.text !== App.eventCategory

    spacing: 16

    function load() {
        nameField.text = App.eventName;
        championshipField.text = App.eventChampionship;
        categoryField.text = App.eventCategory;
        saved = false;
    }

    function save() {
        App.saveEvent(nameField.text, championshipField.text, categoryField.text);
        // Normaliza os campos ao que foi gravado (nome = pasta vira vazio).
        load();
        saved = true;
    }

    Component.onCompleted: load()

    Connections {
        target: App
        function onProjectChanged() {
            // Trocou de projeto (ou gravou): recarrega, sem perder o "Salvo".
            if (!page.saved)
                page.load();
        }
    }

    // ------------------------------------------------------------------ //
    // Título
    // ------------------------------------------------------------------ //
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Button {
            variant: "ghost"
            size: "sm"
            iconName: "arrow-left"
            text: qsTr("Back")
            onClicked: page.backRequested()
        }
        Icon {
            name: "calendar"
            size: 20
            color: Theme.brand400
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Event")
            font.pixelSize: Theme.fontLg
            font.weight: Font.DemiBold
            color: Theme.foreground
        }
    }

    // ------------------------------------------------------------------ //
    // Campos do evento
    // ------------------------------------------------------------------ //
    Card {
        Layout.fillWidth: true
        title: qsTr("Event details")
        description: qsTr("The event name, championship and category. They name the project in the app and fill the Instagram caption.")
        contentSpacing: 16
        enabled: App.hasProject

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 16
            rowSpacing: 16

            // Nome (ocupa a linha toda)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                spacing: 6
                FieldLabel {
                    text: qsTr("Event name")
                    buddy: nameField
                }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: App.projectFolderName
                    onTextChanged: if (activeFocus) page.saved = false
                    onAccepted: page.save()
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Leave empty to use the project folder name (%1).").arg(App.projectFolderName)
                    font.pixelSize: Theme.fontXs
                    color: Theme.mutedForeground
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6
                FieldLabel {
                    text: qsTr("Championship")
                    buddy: championshipField
                }
                TextField {
                    id: championshipField
                    Layout.fillWidth: true
                    onTextChanged: if (activeFocus) page.saved = false
                    onAccepted: page.save()
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6
                FieldLabel {
                    text: qsTr("Category")
                    buddy: categoryField
                }
                TextField {
                    id: categoryField
                    Layout.fillWidth: true
                    onTextChanged: if (activeFocus) page.saved = false
                    onAccepted: page.save()
                }
            }
        }

        RowLayout {
            spacing: 12

            Button {
                text: qsTr("Save")
                disabled: !App.hasProject || !page.dirty
                onClicked: page.save()
            }
            Row {
                visible: page.saved && !page.dirty
                spacing: 4
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "circle-check"
                    size: 14
                    color: Theme.success
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Saved")
                    font.pixelSize: Theme.fontSm
                    color: Theme.success
                }
            }
        }
    }
}
