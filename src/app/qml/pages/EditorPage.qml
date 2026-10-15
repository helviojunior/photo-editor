pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import PhotoEditor

// Tela do editor (pages/Editor.jsx, o Develop do Lightroom):
//
//   70% superior: original | editada | painel de edição
//   30% inferior: filmstrip com a foto atual ao centro
//
// No desktop as três divisões são arrastáveis (PanelLayout, lembradas entre
// sessões): alto/baixo, original/editada e a largura do painel. Abaixo de
// 1024px o layout empilha (as duas fotos numa faixa, o painel embaixo, a
// filmstrip no fim) e a página rola.
//
// O painel da esquerda muda com o modo: crop (o quadro sobre a original),
// seleção (o pincel), camada ativa (a área dela por cima) ou só a original.
//
// Atalhos (useShortcuts.js): → ← trocam de foto (no crop giram o quadro de
// 15 em 15°), Del/Backspace exclui, Ctrl/Cmd+Z desfaz, A Auto, C crop, S
// seleção (dentro dela, conclui), L próxima camada, I versão Instagram;
// na seleção Enter conclui, Esc cancela e [ ] mudam o pincel. Valem só com o
// editor na tela, sem diálogo aberto e sem foco num campo de texto; só as
// setas repetem com a tecla segurada.
Item {
    id: page

    readonly property var current: App.editor.current
    readonly property bool hasCurrent: current.id !== undefined && current.id !== ""
    readonly property bool selecting: App.editor.selecting
    readonly property bool mobile: width < 1024
    readonly property bool empty: App.editor.photos.count === 0

    /// Modo crop: o quadro vai sobre a ORIGINAL e a editada mostra o recorte
    /// ao vivo. Sai ao trocar de foto e com Esc; a versão Instagram ABRE nele.
    property bool cropMode: false
    // Girar encolhe o quadro para ele caber na foto; sem memória, girar e
    // voltar a 0° deixaria o quadro pequeno. ``baseScale`` é o tamanho que a
    // pessoa escolheu (ao entrar no modo ou redimensionando), e todo giro
    // parte dele — o quadro só encolhe o quanto o ângulo exige.
    property real baseScale: 1
    property string _photoId: ""

    // --------------------------------------------------------------------- //
    // Ações
    // --------------------------------------------------------------------- //

    function photoChanged() {
        const id = hasCurrent ? current.id : "";
        if (id === _photoId)
            return;
        _photoId = id;
        cropMode = hasCurrent && current.isInstagram === true;
        if (cropMode)
            baseScale = App.editor.crop.scale;
        if (selecting)
            App.editor.cancelSelection();
    }

    function setCropMode(on) {
        if (on && (!hasCurrent || selecting))
            return;
        cropMode = on;
        if (on)
            baseScale = App.editor.crop.scale;
    }

    /// Quadro novo do CropEditor/painel; o giro parte sempre do baseScale.
    function updateCrop(crop, mode) {
        const next = Object.assign({}, crop);
        if (mode === "rotate")
            next.scale = baseScale;
        else
            baseScale = crop.scale;
        App.editor.setCrop(next);
    }

    /// ← / → no modo crop: gira o quadro para o próximo múltiplo de 15° e grava.
    function rotateCrop(direction) {
        const crop = App.editor.crop;
        const k = crop.angle / 15;
        const target = (direction > 0 ? Math.floor(k + 1e-6) + 1 : Math.ceil(k - 1e-6) - 1) * 15;
        updateCrop(Object.assign({}, crop, { angle: target }), "rotate");
        App.editor.commitCrop();
    }

    /// Modo seleção: camada nova ('') ou a área de uma existente.
    function startSelect(layerId) {
        if (!hasCurrent)
            return;
        if (!layerId && App.editor.layers.length >= App.editor.maxLayers)
            return;
        cropMode = false;
        brush.mode = "add";
        App.editor.beginSelection(layerId);
    }

    function applySelection() {
        if (selecting && !App.editor.selectionBusy)
            App.editor.applySelection();
    }

    function openExport() {
        if (App.editor.exportStatus.running === true)
            exportDialog.open();
        else
            exportDialog.start();
    }

    Component.onCompleted: photoChanged()

    Connections {
        target: App.editor
        function onCurrentChanged() {
            page.photoChanged();
        }
    }

    Brush {
        id: brush
    }

    PanelLayout {
        id: panels
    }

    ExportDialog {
        id: exportDialog
    }

    InstagramPublishDialog {
        id: publishDialog
    }

    // --------------------------------------------------------------------- //
    // Atalhos
    // --------------------------------------------------------------------- //

    readonly property Item _focusItem: Window.activeFocusItem
    // Foco num campo: as teclas são dele (a seta move o slider, o Backspace
    // apaga o texto).
    readonly property bool _textFocus: _focusItem !== null && ("cursorPosition" in _focusItem)
    readonly property bool _arrowFocus: _focusItem !== null
        && (("visualPosition" in _focusItem) || ("displayText" in _focusItem))
    readonly property bool _buttonFocus: _focusItem !== null && ("checkable" in _focusItem) && ("autoRepeat" in _focusItem)
    readonly property bool keysActive: App.page === "editor" && visible && !empty
        && Dialogs.current === null && !exportDialog.visible && !publishDialog.visible && !_textFocus

    Shortcut {
        sequences: ["Right"]
        enabled: page.keysActive && !page._arrowFocus && !page.selecting
        onActivated: page.cropMode ? page.rotateCrop(1) : App.editor.next()
    }
    Shortcut {
        sequences: ["Left"]
        enabled: page.keysActive && !page._arrowFocus && !page.selecting
        onActivated: page.cropMode ? page.rotateCrop(-1) : App.editor.previous()
    }
    // Pintando, nada exclui: um Backspace perdido não pode jogar a foto fora.
    Shortcut {
        sequences: ["Delete", "Backspace"]
        autoRepeat: false
        enabled: page.keysActive && !page.selecting && !App.editor.picking && page.hasCurrent
        onActivated: App.editor.deleteCurrent()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        autoRepeat: false
        enabled: page.keysActive
        onActivated: App.editor.undo()
    }
    Shortcut {
        sequence: "A"
        autoRepeat: false
        enabled: page.keysActive && !page.selecting && page.hasCurrent
        onActivated: App.editor.autoAdjust()
    }
    Shortcut {
        sequence: "C"
        autoRepeat: false
        enabled: page.keysActive && !page.selecting && page.hasCurrent
        onActivated: page.setCropMode(!page.cropMode)
    }
    Shortcut {
        sequence: "S"
        autoRepeat: false
        enabled: page.keysActive && page.hasCurrent
        onActivated: page.selecting ? page.applySelection() : page.startSelect("")
    }
    Shortcut {
        sequence: "L"
        autoRepeat: false
        enabled: page.keysActive && !page.selecting && !page.cropMode
        onActivated: App.editor.cycleLayer()
    }
    Shortcut {
        sequence: "I"
        autoRepeat: false
        enabled: page.keysActive && !page.selecting && !App.editor.picking && page.hasCurrent
        onActivated: App.editor.toggleInstagram()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        autoRepeat: false
        enabled: page.keysActive && page.selecting && !page._buttonFocus
        onActivated: page.applySelection()
    }
    Shortcut {
        sequence: "Esc"
        autoRepeat: false
        enabled: page.keysActive && (page.selecting || page.cropMode)
        onActivated: {
            if (page.selecting)
                App.editor.cancelSelection();
            else
                page.cropMode = false;
        }
    }
    Shortcut {
        sequence: "["
        enabled: page.keysActive && page.selecting
        onActivated: brush.step(-1)
    }
    Shortcut {
        sequence: "]"
        enabled: page.keysActive && page.selecting
        onActivated: brush.step(1)
    }

    // --------------------------------------------------------------------- //
    // Catálogo vazio
    // --------------------------------------------------------------------- //

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 640)
        visible: page.empty
        spacing: 12

        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: "image-off"
            size: 40
            color: Theme.mutedForeground
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No photos in the catalog")
            font.pixelSize: Theme.fontLg
            font.weight: Font.DemiBold
            color: Theme.foreground
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Put the original JPEGs in the raw/ folder of the project and rescan.")
            font.pixelSize: Theme.fontBase
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            variant: "outline"
            iconName: "refresh-cw"
            text: qsTr("Rescan")
            onClicked: App.editor.rescan()
        }
    }

    // --------------------------------------------------------------------- //
    // Layout
    // --------------------------------------------------------------------- //

    Flickable {
        id: scroller

        // Desktop: tudo cabe na janela; celular: empilhado, a página rola.
        readonly property real topHeight: Math.round(panels.top * height)
        readonly property real panelWidth: page.mobile ? width : panels.panel
        readonly property real imagesWidth: page.mobile ? width : Math.max(0, width - 5 - panelWidth)
        readonly property real imagesHeight: page.mobile ? Math.round(height * 0.42) : topHeight
        readonly property real panelHeight: page.mobile ? panelColumn.implicitHeight + 1 : topHeight
        readonly property real stripTop: page.mobile ? imagesHeight + panelHeight + 1 : topHeight + 5
        readonly property real stripHeight: page.mobile ? 176 : Math.max(0, height - topHeight - 5)

        anchors.fill: parent
        visible: !page.empty
        clip: true
        interactive: page.mobile
        contentWidth: width
        contentHeight: page.mobile ? stripTop + stripHeight : height
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: ThinScrollBar {}

        // Original | Editada
        Rectangle {
            id: imagesArea
            x: 0
            y: 0
            width: scroller.imagesWidth
            height: scroller.imagesHeight
            color: Theme.border

            readonly property real gap: page.mobile ? 1 : 5
            readonly property real leftWidth: page.mobile
                ? Math.floor((width - gap) / 2)
                : Math.round(panels.split * (width - gap))

            Item {
                id: leftPane
                x: 0
                width: imagesArea.leftWidth
                height: parent.height

                CropEditor {
                    anchors.fill: parent
                    visible: page.cropMode && page.hasCurrent
                    label: qsTr("Original")
                    source: App.editor.originalUrl
                    aspect: page.current.aspect || 1
                    crop: App.editor.crop
                    onEdited: (crop, mode) => page.updateCrop(crop, mode)
                    onCommitted: App.editor.commitCrop()
                }

                SelectEditor {
                    anchors.fill: parent
                    visible: !page.cropMode
                    label: qsTr("Original")
                    source: App.editor.originalUrl
                    aspect: page.current.aspect || 1
                    editable: page.selecting
                    brush: brush
                    busy: page.selecting && App.editor.selectionBusy
                    maskUrl: page.selecting ? App.editor.selectionMaskUrl : App.editor.activeLayerMaskUrl
                    faint: !page.selecting
                    onStroked: (points, radius, mode) => App.editor.stroke(points, radius, mode, brush.smart)
                }
            }

            Splitter {
                visible: !page.mobile
                x: leftPane.width
                width: 5
                height: parent.height
                container: imagesArea
                mode: "fraction"
                label: qsTr("Resize original and edited (double-click to reset)")
                value: panels.split
                from: panels.limits.split[0]
                to: panels.limits.split[1]
                stepSize: panels.steps.split
                defaultValue: panels.defaults.split
                onMoved: (v) => panels.set("split", v)
            }

            ImagePane {
                x: leftPane.width + imagesArea.gap
                width: imagesArea.width - x
                height: parent.height
                label: qsTr("Edited")
                source: App.editor.editedUrl
            }
        }

        Splitter {
            id: topArea
            visible: !page.mobile
            x: imagesArea.width
            width: 5
            height: scroller.topHeight
            container: scroller
            mode: "pixelsFromEnd"
            label: qsTr("Resize the edit panel (double-click to reset)")
            value: panels.panel
            from: panels.limits.panel[0]
            to: panels.limits.panel[1]
            stepSize: -panels.steps.panel
            defaultValue: panels.defaults.panel
            onMoved: (v) => panels.set("panel", v)
        }

        // Painel de edição
        Rectangle {
            id: panel
            x: page.mobile ? 0 : scroller.width - width
            y: page.mobile ? scroller.imagesHeight : 0
            width: scroller.panelWidth
            height: scroller.panelHeight
            color: Theme.card

            Rectangle {
                visible: page.mobile
                width: parent.width
                height: 1
                color: Theme.border
            }

            Flickable {
                id: panelScroll
                anchors.fill: parent
                anchors.topMargin: page.mobile ? 1 : 0
                clip: true
                interactive: !page.mobile
                contentWidth: width
                contentHeight: panelColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: ThinScrollBar {}

                ColumnLayout {
                    id: panelColumn
                    width: panelScroll.width
                    spacing: 0

                    // A foto: nome, tamanho, data; merge e versão Instagram.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        visible: page.hasCurrent
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: page.current.fileName || ""
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                            color: Theme.foreground
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                const c = page.current;
                                let s = qsTr("%1 × %2").arg(c.width || 0).arg(c.height || 0);
                                if (c.capturedAt)
                                    s += " · " + c.capturedAt.toLocaleString(
                                        Qt.locale(String(App.language).replace("-", "_")), Locale.ShortFormat);
                                return s;
                            }
                            font.pixelSize: Theme.fontSm
                            color: Theme.mutedForeground
                            elide: Text.ElideRight
                        }
                        Button {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            visible: page.current.isMergeBase === true
                            variant: "outline"
                            size: "sm"
                            iconName: "combine"
                            text: qsTr("Edit merge")
                            onClicked: App.openMerge(page.current.mergeId)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            visible: page.current.isInstagram === true
                            spacing: 6
                            Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 1
                                name: "instagram"
                                size: 14
                                color: Theme.brand400
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Instagram version: exported to publicar/instagram/ in the feed format.")
                                font.pixelSize: Theme.fontSm
                                color: Theme.mutedForeground
                                wrapMode: Text.Wrap
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            visible: page.current.isInstagram === true
                            variant: "outline"
                            size: "sm"
                            iconName: "chevron-left"
                            text: qsTr("Back to the photo (I)")
                            onClicked: App.editor.toggleInstagram()
                        }
                    }
                    Separator {
                        Layout.fillWidth: true
                        visible: page.hasCurrent
                    }

                    SelectionPanel {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        visible: page.selecting
                        brush: brush
                        editing: App.editor.selectionLayer !== ""
                        hasMask: App.editor.selectionMaskUrl !== ""
                        coverage: App.editor.selectionCoverage
                        busy: App.editor.selectionBusy
                        smartAvailable: App.editor.smartAvailable
                        onApplyRequested: page.applySelection()
                        onClearRequested: App.editor.clearSelection()
                        onCancelRequested: App.editor.cancelSelection()
                    }

                    EditPanel {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        visible: !page.selecting
                        disabled: !page.hasCurrent
                        cropMode: page.cropMode
                        onCropToggled: page.setCropMode(!page.cropMode)
                        onCropEdited: (crop, mode) => page.updateCrop(crop, mode)
                        onCropCommitted: App.editor.commitCrop()
                        onCropRestarted: page.baseScale = App.editor.crop.scale
                        onSelectRequested: (layerId) => page.startSelect(layerId)
                    }

                    Separator {
                        Layout.fillWidth: true
                    }
                    PhotoHistory {
                        Layout.fillWidth: true
                        Layout.margins: 16
                    }
                }
            }
        }

        Splitter {
            visible: !page.mobile
            y: scroller.topHeight
            width: scroller.width
            height: 5
            orientation: Qt.Horizontal
            container: scroller
            mode: "fraction"
            label: qsTr("Resize the filmstrip (double-click to reset)")
            value: panels.top
            from: panels.limits.top[0]
            to: panels.limits.top[1]
            stepSize: panels.steps.top
            defaultValue: panels.defaults.top
            onMoved: (v) => panels.set("top", v)
        }

        Rectangle {
            visible: page.mobile
            y: scroller.imagesHeight + scroller.panelHeight
            width: scroller.width
            height: 1
            color: Theme.border
        }

        // Filmstrip
        Rectangle {
            y: scroller.stripTop
            width: scroller.width
            height: scroller.stripHeight
            color: Theme.card

            FilmstripToolbar {
                id: toolbar
                width: parent.width
                onExportRequested: page.openExport()
                onPublishRequested: publishDialog.open()
            }

            Filmstrip {
                anchors.top: toolbar.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 8
                anchors.rightMargin: 8
            }
        }
    }
}
