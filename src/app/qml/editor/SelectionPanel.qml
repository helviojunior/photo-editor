import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Controles do modo seleção (SelectionPanel do LayersPanel.jsx), no lugar do
// painel de edição enquanto ele dura: somar/subtrair, tamanho do pincel,
// detecção do objeto (IA) e concluir. Serve ao editor e ao merge — quem usa
// liga ``editing``/``hasMask``/``coverage``/``busy`` ao controller dele.
ColumnLayout {
    id: panel

    property Brush brush: null
    /// Editando a área de uma camada que já existe (senão: criando uma).
    property bool editing: false
    property bool hasMask: false
    property real coverage: 0
    property bool busy: false
    property bool smartAvailable: false

    signal applyRequested()
    signal clearRequested()
    signal cancelRequested()

    readonly property bool empty: !hasMask
    readonly property real percent: Math.round(coverage * 1000) / 10

    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        SectionTitle {
            text: panel.editing ? qsTr("Edit area") : qsTr("Select area")
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Paint over the object to extract it. Each stroke adds to the selection; Subtract (or holding Alt) removes. [ and ] change the brush size. S or Enter finishes, Esc cancels.")
            font.pixelSize: Theme.fontXs
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 8
        rowSpacing: 8
        Accessible.name: qsTr("Brush mode")

        Button {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            size: "sm"
            iconName: "brush"
            text: qsTr("Add")
            variant: panel.brush && panel.brush.mode === "add" ? "default" : "outline"
            onClicked: panel.brush.mode = "add"
        }
        Button {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            size: "sm"
            iconName: "eraser"
            text: qsTr("Subtract")
            variant: panel.brush && panel.brush.mode === "subtract" ? "default" : "outline"
            onClicked: panel.brush.mode = "subtract"
        }
    }

    ValueSlider {
        Layout.fillWidth: true
        label: qsTr("Brush size")
        from: panel.brush ? panel.brush.minimum : 0
        to: panel.brush ? panel.brush.maximum : 1
        stepSize: panel.brush ? panel.brush.stepSize : 0.005
        value: panel.brush ? panel.brush.size : 0
        resetOnDoubleClick: false
        valueText: (Math.round(value * 1000) / 10) + "%"
        onMoved: (v) => panel.brush.size = Math.round(v * 1000) / 1000
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Toggle {
            checked: panel.brush !== null && panel.brush.smart && panel.smartAvailable
            disabled: !panel.smartAvailable
            label: qsTr("Detect the object (AI)")
            onToggled: (value) => panel.brush.smart = value
        }
        Text {
            Layout.fillWidth: true
            text: panel.smartAvailable
                ? qsTr("On: the stroke becomes the whole object under it. Off: exactly the painted area.")
                : qsTr("The detection model is not installed; the brush selects exactly the painted area.")
            font.pixelSize: Theme.fontXs
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }
    }

    Text {
        Layout.fillWidth: true
        text: panel.busy ? qsTr("Detecting…")
            : panel.empty ? qsTr("Nothing selected yet.")
            : qsTr("%1% of the photo selected").arg(panel.percent.toLocaleString(Qt.locale(), "f", panel.percent % 1 ? 1 : 0))
        font.pixelSize: Theme.fontSm
        color: Theme.mutedForeground
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StatusBar
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 8
        rowSpacing: 8

        Button {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            size: "sm"
            iconName: "check"
            text: panel.editing ? qsTr("Apply") : qsTr("Create layer")
            disabled: panel.busy || (panel.empty && !panel.editing)
            onClicked: panel.applyRequested()
        }
        Button {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            size: "sm"
            variant: "outline"
            iconName: "eraser"
            text: qsTr("Clear")
            disabled: panel.busy || panel.empty
            onClicked: panel.clearRequested()
        }
        Button {
            Layout.fillWidth: true
            Layout.columnSpan: 2
            size: "sm"
            variant: "ghost"
            iconName: "x"
            text: qsTr("Cancel")
            onClicked: panel.cancelRequested()
        }
    }
}
