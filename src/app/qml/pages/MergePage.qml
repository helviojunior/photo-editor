pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import PhotoEditor

// Tela do merge (pages/Merge.jsx): áreas de várias fotos, alinhadas, em
// camadas sobre a foto base — a trajetória da bola a partir de uma sequência.
//
//   esquerda: a foto da camada ativa (com o pincel, no modo seleção)
//   direita:  o resultado — a base e, por cima, a área de cada camada
//   painel:   transparência e camadas (MergePanel)
//
// O C++ (App.merge) já alinhou cada foto à base ao criar o merge e realinha
// sem a área quando ela muda; transparência e visibilidade gravam sozinhas.
// "← Editor" grava o que falta e volta ao editor na base.
//
// Atalhos (Merge.jsx; teclas do Keymap, as mesmas da seleção do editor): S
// seleciona a área da camada ativa; na seleção Enter (ou S) conclui, Esc
// cancela e [ ] mudam o pincel.
Item {
    id: page

    readonly property var merge: App.merge
    readonly property bool selecting: merge.selecting
    readonly property var base: merge.base
    readonly property var baseLayer: merge.baseLayer
    readonly property var layers: merge.layers
    readonly property bool mobile: width < 1024
    readonly property bool baseActive: merge.activePhoto !== "" && merge.activePhoto === base.id
    readonly property var activeLayer: {
        if (baseActive)
            return { photoId: base.id, fileName: base.fileName, maskUrl: baseLayer.maskUrl };
        for (const l of layers) {
            if (l.photoId === merge.activePhoto)
                return l;
        }
        return null;
    }
    readonly property int readyCount: layers.filter((l) => l.visible && l.hasMask).length

    // O que o resultado mostra: o fundo por trás do objeto da base (quanto
    // mais claro o objeto, mais forte o fundo), a área de cada camada visível
    // e, na que está sendo selecionada, a seleção em construção.
    readonly property var pieces: {
        const out = [];
        const live = selecting ? merge.selectionCutoutUrl : "";
        const baseUrl = selecting && baseActive ? live : baseLayer.cutoutUrl;
        if (baseUrl && baseLayer.opacity < 1)
            out.push({ key: "base", url: baseUrl, opacity: 1 - baseLayer.opacity });
        for (const l of layers) {
            const url = selecting && l.photoId === merge.activePhoto ? live : l.cutoutUrl;
            if (l.visible && url)
                out.push({ key: l.photoId, url: url, opacity: l.opacity });
        }
        return out;
    }

    function startSelect(photoId) {
        if (!photoId)
            return;
        merge.activePhoto = photoId;
        if (merge.activePhoto !== photoId)
            return;
        brush.mode = "add";
        merge.beginSelection();
    }

    function applySelection() {
        if (selecting && !merge.selectionBusy)
            merge.applySelection();
    }

    function removeLayer(photoId, fileName) {
        const last = layers.length <= 1;
        const name = "<b>" + Messages.escapeHtml(fileName) + "</b>";
        Dialogs.confirm({
            title: qsTr("Remove from the merge?"),
            description: last
                ? qsTr("%1 is the last layer: removing it undoes the merge, and the photos go back to the filmstrip.").arg(name)
                : qsTr("%1 leaves the composition and goes back to the filmstrip.").arg(name),
            variant: "danger",
            confirmLabel: qsTr("Remove"),
            onConfirm: () => App.merge.removeLayer(photoId)
        });
    }

    function dissolve() {
        Dialogs.confirm({
            title: qsTr("Undo the merge?"),
            description: qsTr("The base goes back to being just its own photo, and the %1 layer photos return to the filmstrip. The selected areas are lost.").arg(layers.length),
            variant: "danger",
            confirmLabel: qsTr("Undo merge"),
            onConfirm: () => App.merge.dissolve()
        });
    }

    Brush {
        id: brush
        size: 0.015
    }

    // --------------------------------------------------------------------- //
    // Atalhos
    // --------------------------------------------------------------------- //

    readonly property Item _focusItem: Window.activeFocusItem
    readonly property bool _textFocus: _focusItem !== null && ("cursorPosition" in _focusItem)
    readonly property bool _buttonFocus: _focusItem !== null && ("checkable" in _focusItem) && ("autoRepeat" in _focusItem)
    readonly property bool keysActive: App.page === "merge" && visible && merge.open && !Keymap.recording
        && Dialogs.current === null && !_textFocus

    Shortcut {
        sequences: Keymap.keys("selectArea")
        autoRepeat: false
        enabled: page.keysActive && page.activeLayer !== null
        onActivated: page.selecting ? page.applySelection() : page.startSelect(page.merge.activePhoto)
    }
    Shortcut {
        sequences: Keymap.keys("applySelection")
        autoRepeat: false
        enabled: page.keysActive && page.selecting && !page._buttonFocus
        onActivated: page.applySelection()
    }
    Shortcut {
        sequences: Keymap.keys("cancel")
        autoRepeat: false
        enabled: page.keysActive && page.selecting
        onActivated: App.merge.cancelSelection()
    }
    Shortcut {
        sequences: Keymap.keys("brushSmaller")
        enabled: page.keysActive && page.selecting
        onActivated: brush.step(-1)
    }
    Shortcut {
        sequences: Keymap.keys("brushLarger")
        enabled: page.keysActive && page.selecting
        onActivated: brush.step(1)
    }

    // --------------------------------------------------------------------- //
    // Layout
    // --------------------------------------------------------------------- //

    // Barra de cima: voltar, título, dica e desfazer.
    Rectangle {
        id: topBar
        width: parent.width
        implicitHeight: Math.max(44, topRow.implicitHeight + 8)
        height: implicitHeight
        color: Theme.card

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            id: topRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.bottomMargin: 1
            spacing: 8

            Button {
                size: "sm"
                variant: "ghost"
                iconName: "arrow-left"
                text: qsTr("Editor")
                disabled: !page.merge.open
                onClicked: App.merge.close()
            }
            Text {
                Layout.maximumWidth: topRow.width * 0.4
                text: page.merge.open ? qsTr("Merge · %1").arg(page.base.fileName) : qsTr("Loading…")
                font.pixelSize: Theme.fontBase
                font.weight: Font.DemiBold
                color: Theme.foreground
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                visible: page.merge.open && topRow.width > 640
                text: page.readyCount
                    ? qsTr("Export saves it as publicar/%1").arg(page.merge.exportName)
                    : qsTr("Select the area of at least one layer")
                font.pixelSize: Theme.fontSm
                color: Theme.mutedForeground
                elide: Text.ElideRight
            }
            Item {
                Layout.fillWidth: true
                visible: !(page.merge.open && topRow.width > 640)
            }
            Button {
                size: "sm"
                variant: "ghost"
                iconName: "unlink"
                text: qsTr("Undo merge")
                disabled: !page.merge.open || page.selecting
                onClicked: page.dissolve()
            }
        }
    }

    Flickable {
        id: scroller

        readonly property real panelWidth: page.mobile ? width : 320
        readonly property real imagesWidth: page.mobile ? width : width - panelWidth
        readonly property real imagesHeight: page.mobile ? Math.round(page.height * 0.42) : height
        readonly property real panelHeight: page.mobile ? panelColumn.implicitHeight + 33 : height

        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        interactive: page.mobile
        contentWidth: width
        contentHeight: page.mobile ? imagesHeight + panelHeight : height
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: ThinScrollBar {}

        Rectangle {
            id: imagesArea
            width: scroller.imagesWidth
            height: scroller.imagesHeight
            color: Theme.border

            readonly property real leftWidth: Math.floor((width - 1) / 2)

            SelectEditor {
                width: imagesArea.leftWidth
                height: parent.height
                label: page.activeLayer
                    ? (page.baseActive ? qsTr("Base") + " · " + page.activeLayer.fileName : page.activeLayer.fileName)
                    : (page.merge.open ? qsTr("Base") + " · " + page.base.fileName : "")
                source: page.activeLayer ? page.merge.activePreviewUrl : (page.base.previewUrl || "")
                aspect: page.base.width ? page.base.height / page.base.width : 2 / 3
                editable: page.selecting
                brush: brush
                busy: page.selecting && page.merge.selectionBusy
                maskUrl: page.selecting ? page.merge.selectionMaskUrl : (page.activeLayer ? page.activeLayer.maskUrl || "" : "")
                faint: !page.selecting
                onStroked: (points, radius, mode) => App.merge.stroke(points, radius, mode, brush.smart)
            }

            CompositePane {
                x: imagesArea.leftWidth + 1
                width: imagesArea.width - x
                height: parent.height
                label: qsTr("Result")
                baseUrl: page.base.previewUrl || ""
                aspect: page.base.width ? page.base.height / page.base.width : 2 / 3
                pieces: page.pieces
                busy: page.merge.busy
            }
        }

        Rectangle {
            id: panel
            x: page.mobile ? 0 : imagesArea.width
            y: page.mobile ? imagesArea.height : 0
            width: scroller.panelWidth
            height: scroller.panelHeight
            color: Theme.card

            // Borda: à esquerda no desktop, em cima no celular.
            Rectangle {
                width: page.mobile ? parent.width : 1
                height: page.mobile ? 1 : parent.height
                color: Theme.border
            }

            Flickable {
                id: panelScroll
                anchors.fill: parent
                anchors.leftMargin: page.mobile ? 0 : 1
                anchors.topMargin: page.mobile ? 1 : 0
                clip: true
                interactive: !page.mobile
                contentWidth: width
                contentHeight: panelColumn.implicitHeight + 32
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: ThinScrollBar {}

                ColumnLayout {
                    id: panelColumn
                    x: 16
                    y: 16
                    width: panelScroll.width - 32
                    spacing: 0

                    SelectionPanel {
                        Layout.fillWidth: true
                        visible: page.selecting
                        brush: brush
                        editing: true
                        hasMask: page.merge.selectionMaskUrl !== ""
                        coverage: page.merge.selectionCoverage
                        busy: page.merge.selectionBusy
                        smartAvailable: App.editor.smartAvailable
                        onApplyRequested: page.applySelection()
                        onClearRequested: App.merge.clearSelection()
                        onCancelRequested: App.merge.cancelSelection()
                    }

                    MergePanel {
                        Layout.fillWidth: true
                        visible: !page.selecting && page.merge.open
                        onSelectRequested: (photoId) => page.startSelect(photoId)
                        onRemoveRequested: (photoId, fileName) => page.removeLayer(photoId, fileName)
                    }
                }
            }
        }
    }
}
