pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Quem desenha os diálogos pedidos ao singleton ``Dialogs``. Instanciado UMA
// vez, no Main.qml; o resto do app só chama Dialogs.confirm/alert/choose.
Modal {
    id: host

    readonly property var dialog: Dialogs.current
    readonly property bool busy: Dialogs.busy

    title: dialog ? dialog.title || "" : ""
    description: dialog ? dialog.description || "" : ""
    variant: dialog ? dialog.variant || "info" : "info"
    showIcon: true
    iconName: dialog && dialog.iconName ? dialog.iconName : ""
    size: dialog && dialog.size ? dialog.size : "sm"
    dismissible: !busy
    // Mais de duas opções: empilhadas, a principal em cima.
    footerVertical: dialog !== null && dialog.buttons.length > 2

    onDialogChanged: {
        if (dialog && !opened)
            open();
        else if (!dialog && visible)
            close();
    }

    // Esc, clique fora ou X: é o "Cancelar".
    onClosed: Dialogs._dismiss()

    footer: Repeater {
        model: host.dialog ? host.dialog.buttons : []

        Button {
            id: dialogButton
            required property var modelData
            required property int index

            Layout.fillWidth: host.footerVertical
            text: dialogButton.modelData.label
            variant: dialogButton.modelData.variant || "default"
            iconName: dialogButton.modelData.iconName || ""
            loading: host.busy && dialogButton.modelData.id === Dialogs.busyButton
            disabled: host.busy && dialogButton.modelData.id !== Dialogs.busyButton
            focus: dialogButton.modelData.primary === true
            onClicked: Dialogs._press(dialogButton.modelData.id)

            Component.onCompleted: {
                if (dialogButton.modelData.primary === true)
                    forceActiveFocus();
            }
        }
    }
}
