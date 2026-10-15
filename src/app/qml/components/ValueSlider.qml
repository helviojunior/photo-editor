import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import PhotoEditor

// Controle deslizante dos painéis (sliders do EditPanel.jsx): rótulo à
// esquerda, valor à direita (algarismos tabulares) e o trilho embaixo, com o
// preenchimento no acento da marca.
//
//   ValueSlider {
//       Layout.fillWidth: true
//       label: qsTr("Exposure")
//       from: -5; to: 5; stepSize: 0.05
//       value: App.editor.values.exposure
//       onMoved: (v) => App.editor.setValue("exposure", v)   // ao vivo
//       onReleased: App.editor.commit()                       // grava
//   }
//
// - Duplo clique no rótulo volta a ``defaultValue`` (como no Lightroom):
//   emite moved(defaultValue) e released().
// - Setas mudam um passo; segurar a seta é UM ajuste: released() sai quando
//   o teclado para por 600 ms.
// - Clicar não rouba o foco do teclado (focusPolicy Tab): as setas da
//   página (trocar de foto) continuam valendo depois de arrastar.
// - É controlado: ``value`` vem de quem usa; o arraste só emite moved().
Item {
    id: root

    property string label: ""
    property real value: 0
    property real from: -100
    property real to: 100
    property real stepSize: 1
    property real defaultValue: 0
    /// Casas decimais do valor mostrado (padrão: 2 se o passo é fracionário).
    property int decimals: stepSize < 1 ? 2 : 0
    /// "+" na frente dos positivos (ajustes bipolares).
    property bool showSign: true
    property string suffix: ""
    /// Texto do valor; por padrão o número formatado.
    property string valueText: {
        const text = root.value.toFixed(root.decimals);
        return (root.showSign && root.value > 0 ? "+" : "") + text + root.suffix;
    }
    property bool resetOnDoubleClick: true
    property bool disabled: false
    readonly property bool pressed: slider.pressed

    signal moved(real value)
    signal released()

    implicitWidth: 200
    implicitHeight: layout.implicitHeight + 8
    opacity: disabled ? 0.5 : 1

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                id: labelText
                Layout.fillWidth: true
                text: root.label
                font.pixelSize: Theme.fontSm
                color: Theme.foreground
                elide: Text.ElideRight

                MouseArea {
                    id: labelMouse
                    anchors.fill: parent
                    hoverEnabled: root.resetOnDoubleClick
                    enabled: !root.disabled
                    onDoubleClicked: {
                        if (!root.resetOnDoubleClick)
                            return;
                        root.moved(root.defaultValue);
                        root.released();
                    }
                }
                Tip {
                    text: qsTr("Double-click to reset")
                    visible: root.resetOnDoubleClick && labelMouse.containsMouse
                }
            }

            Text {
                text: root.valueText
                font.pixelSize: Theme.fontSm
                font.features: { "tnum": 1 }
                color: Math.abs(root.value - root.defaultValue) > 1e-9 ? Theme.foreground : Theme.mutedForeground
            }
        }

        T.Slider {
            id: slider
            Layout.fillWidth: true
            implicitHeight: 22
            from: root.from
            to: root.to
            stepSize: root.stepSize
            snapMode: T.Slider.SnapAlways
            value: root.value
            enabled: !root.disabled
            focusPolicy: Qt.TabFocus
            live: true
            padding: 0
            leftPadding: 9
            rightPadding: 9

            Accessible.name: root.label

            onMoved: root.moved(value)
            onPressedChanged: {
                if (!pressed)
                    root.released();
            }

            Keys.onReleased: (event) => {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                        || event.key === Qt.Key_Up || event.key === Qt.Key_Down
                        || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown)
                    keyTimer.restart();
                event.accepted = false;
            }

            background: Item {
                x: slider.leftPadding
                width: slider.availableWidth
                height: slider.height

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 4
                    radius: 2
                    color: Theme.sliderTrack
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: slider.visualPosition * parent.width
                    height: 4
                    radius: 2
                    color: Theme.sliderFill
                }
            }

            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * slider.availableWidth - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: Theme.sliderHandle
                border.width: 1
                border.color: Theme.sliderHandleBorder
                scale: slider.pressed ? 1.08 : 1
                Behavior on scale { NumberAnimation { duration: 100 } }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focusRing
                    visible: slider.activeFocus
                }
            }
        }
    }

    Timer {
        id: keyTimer
        interval: 600
        onTriggered: root.released()
    }
}
