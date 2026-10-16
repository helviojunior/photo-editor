import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Campo numérico compacto dos Níveis e das Curvas (as caixinhas do
// Photoshop): mostra ``value`` enquanto não está sendo editado; Enter, Tab
// ou sair do campo emite ``edited(v)`` com o número limitado a [from, to].
// ↑/↓ mudam um ``step`` e emitem na hora. Valor inválido volta ao atual.
//
//   NumberField { value: lv.inBlack; from: 0; to: 253; onEdited: (v) => set({ inBlack: v }) }
T.TextField {
    id: control

    property real value: 0
    property real from: 0
    property real to: 255
    property int decimals: 0
    property real step: decimals > 0 ? 0.01 : 1

    signal edited(real value)

    function format(v) {
        return Number(v).toFixed(control.decimals);
    }
    function submit(v) {
        const n = Math.min(control.to, Math.max(control.from, v));
        control.text = format(n);
        if (Math.abs(n - control.value) > 1e-9)
            control.edited(n);
    }

    implicitWidth: 52
    implicitHeight: 26
    leftPadding: 6
    rightPadding: 6
    topPadding: 0
    bottomPadding: 0
    horizontalAlignment: TextInput.AlignHCenter
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: Theme.fontSm
    font.features: { "tnum": 1 }
    color: Theme.foreground
    selectionColor: Theme.alpha(Theme.brand400, 0.45)
    selectedTextColor: Theme.foreground
    selectByMouse: true
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    validator: RegularExpressionValidator { regularExpression: /^-?[0-9]*[.,]?[0-9]*$/ }
    text: format(value)

    onValueChanged: {
        if (!activeFocus)
            text = format(value);
    }
    onActiveFocusChanged: {
        if (activeFocus)
            selectAll();
        else
            text = format(value);
    }
    onEditingFinished: {
        const v = Number(String(text).replace(",", "."));
        if (text === "" || !isFinite(v))
            text = format(value);
        else
            submit(v);
    }
    Keys.onUpPressed: submit(value + step)
    Keys.onDownPressed: submit(value - step)

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.background
        border.color: control.activeFocus ? Theme.focusRing : Theme.input
    }
}
