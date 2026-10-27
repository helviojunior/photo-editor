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
// A filmstrip mostra uma de três visualizações (App.editor.view): as fotos,
// as arquivadas ou a lixeira. Só nas fotos se edita; nas outras o painel
// mostra a foto, um aviso de onde ela está e o Restaurar — sem sliders, crop
// nem seleção (a Editada continua mostrando os ajustes gravados).
//
// Atalhos (padrões do Keymap, trocáveis no editor de atalhos): → ← trocam de
// foto (no crop giram o quadro de 15 em 15°), Backspace arquiva, Del manda
// para a lixeira (das fotos e das arquivadas; na lixeira nenhuma tecla exclui
// de vez — só os botões, com confirmação digitada), Ctrl/Cmd+Shift+R
// restaura, Ctrl/Cmd+Z
// desfaz, Ctrl/Cmd+Shift+Z (ou +Y) refaz, Ctrl/Cmd+D duplica, A Auto, C crop,
// S seleção (dentro dela, conclui), L próxima camada, I versão Instagram,
// W conta-gotas do balanço de branco (Esc cancela), Alt+1..4 as abas do painel;
// na seleção Enter conclui, Esc cancela e [ ] mudam o pincel. Valem só com o
// editor na tela, sem diálogo aberto e sem foco num campo de texto; só as
// setas repetem com a tecla segurada. ``run(id)`` é o que cada um faz — a
// paleta de comandos chama o mesmo.
Item {
    id: page

    readonly property var current: App.editor.current
    readonly property bool hasCurrent: current.id !== undefined && current.id !== ""
    readonly property bool selecting: App.editor.selecting
    readonly property bool mobile: width < 1024
    readonly property string view: App.editor.view
    /// Gestão (arquivar, duplicar, capa, merge, rescan) e o layout da filmstrip:
    /// só nas "Fotos".
    readonly property bool editable: view === "photos"
    /// Revelação (ajustes, crop, níveis/curvas/efeitos, seleção): também na
    /// visualização "Instagram", editando a versão no lugar, sem trocar de filtro.
    readonly property bool editing: view === "photos" || view === "instagram"
    /// A visualização atual não tem foto nenhuma.
    readonly property bool listEmpty: App.editor.photos.count === 0
    /// O projeto não tem foto nenhuma, em lugar nenhum: a tela inteira vira o
    /// convite para pôr JPEGs em raw/. Com arquivadas ou lixeira, a barra
    /// fica (é por ela que se chega até elas).
    readonly property bool empty: listEmpty && editable
        && App.editor.archivedCount === 0 && App.editor.trashCount === 0

    /// Modo crop: o quadro vai sobre a ORIGINAL e a editada mostra o recorte
    /// ao vivo. Sai ao trocar de foto e com Esc; a versão Instagram ABRE nele.
    property bool cropMode: false
    // Girar encolhe o quadro para ele caber na foto; sem memória, girar e
    // voltar a 0° deixaria o quadro pequeno. ``baseScale`` é o tamanho que a
    // pessoa escolheu (ao entrar no modo ou redimensionando), e todo giro
    // parte dele — o quadro só encolhe o quanto o ângulo exige.
    property real baseScale: 1
    property string _photoId: ""
    /// Conta-gotas do balanço de branco: o cursor vira cruz na ORIGINAL e um
    /// clique mede (App.editor.pickWhite). Um clique e sai; Esc cancela.
    property bool whitePicking: false

    // --------------------------------------------------------------------- //
    // Ações
    // --------------------------------------------------------------------- //

    function photoChanged() {
        const id = hasCurrent ? current.id : "";
        if (id === _photoId)
            return;
        _photoId = id;
        whitePicking = false;
        cropMode = hasCurrent && editing && current.isInstagram === true;
        if (cropMode)
            baseScale = App.editor.crop.scale;
        if (selecting)
            App.editor.cancelSelection();
    }

    function setCropMode(on) {
        if (on && (!hasCurrent || selecting || !editing))
            return;
        cropMode = on;
        if (on) {
            baseScale = App.editor.crop.scale;
            whitePicking = false;
        }
    }

    function setWhitePicking(on) {
        if (on && (!hasCurrent || selecting || !editing))
            return;
        if (on)
            cropMode = false;
        whitePicking = on;
    }

    function pickWhite(x, y) {
        whitePicking = false;
        App.editor.pickWhite(x, y);
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
        if (!hasCurrent || !editing)
            return;
        if (!layerId && App.editor.layers.length >= App.editor.maxLayers)
            return;
        cropMode = false;
        whitePicking = false;
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

    function openPublish() {
        publishDialog.open();
    }

    /// Exclusão DEFINITIVA da foto atual da lixeira: só depois de digitar a palavra.
    function confirmPurge() {
        if (view !== "trash" || !hasCurrent)
            return;
        Dialogs.confirm({
            title: qsTr("Delete permanently?"),
            description: qsTr("<b>%1</b> will be deleted from the disk and from the catalog, with its adjustments and copies. This cannot be undone.")
                .arg(Messages.escapeHtml(current.fileName)),
            variant: "danger",
            confirmLabel: qsTr("Delete permanently"),
            requireText: qsTr("delete", "word typed to confirm a permanent deletion"),
            onConfirm: () => App.editor.purgeCurrent()
        });
    }

    /// Esvaziar a lixeira: digita-se o NÚMERO de fotos (de propósito diferente
    /// da palavra de uma foto só — quem apaga 40 vê o 40).
    function confirmEmptyTrash() {
        const n = App.editor.trashCount;
        if (n <= 0)
            return;
        Dialogs.confirm({
            title: qsTr("Empty the trash?"),
            description: qsTr("%n photo(s) will be deleted from the disk and from the catalog, with their adjustments and copies. This cannot be undone.", "", n),
            variant: "danger",
            confirmLabel: qsTr("Empty trash"),
            requireText: String(n),
            requirePrompt: qsTr("Type %1 to permanently delete %n photo(s).", "", n).arg("<b>" + n + "</b>"),
            onConfirm: () => App.editor.emptyTrash()
        });
    }

    /// Uma ação do editor pelo id do Keymap: o que os atalhos e a paleta de
    /// comandos (Main.qml) disparam. Quem chama confere as condições.
    function run(id) {
        switch (id) {
        case "nextPhoto": cropMode ? rotateCrop(1) : App.editor.next(); break;
        case "previousPhoto": cropMode ? rotateCrop(-1) : App.editor.previous(); break;
        case "deletePhoto": if (view !== "trash") App.editor.deleteCurrent(); break;
        case "archivePhoto": if (editable) App.editor.archiveCurrent(); break;
        case "restorePhoto": if (!editable) App.editor.restoreCurrent(); break;
        case "purgePhoto": confirmPurge(); break;
        case "emptyTrash": confirmEmptyTrash(); break;
        case "showPhotos": App.editor.view = "photos"; break;
        case "showInstagram": App.editor.view = "instagram"; break;
        case "showArchived": App.editor.view = "archived"; break;
        case "showTrash": App.editor.view = "trash"; break;
        case "duplicate": App.editor.duplicateCurrent(); break;
        case "cover": App.editor.toggleCover(); break;
        case "instagram": App.editor.toggleInstagram(); break;
        case "removeInstagram": App.editor.removeInstagramVersion(); break;
        case "rescan": App.editor.rescan(); break;
        case "merge": App.editor.picking = !App.editor.picking; break;
        case "undo": App.editor.undo(); break;
        case "redo": App.editor.redo(); break;
        case "auto": App.editor.autoAdjust(); break;
        case "reset": App.editor.reset(); break;
        case "crop": setCropMode(!cropMode); break;
        case "nextLayer": App.editor.cycleLayer(); break;
        case "whitePicker": setWhitePicking(!whitePicking); break;
        case "tabGeneral": editPanel.tab = "general"; break;
        case "tabLevels": editPanel.tab = "levels"; break;
        case "tabCurves": editPanel.tab = "curves"; break;
        case "tabEffects": editPanel.tab = "effects"; break;
        case "selectArea": selecting ? applySelection() : startSelect(""); break;
        case "applySelection": applySelection(); break;
        case "cancel":
            if (selecting)
                App.editor.cancelSelection();
            else if (whitePicking)
                whitePicking = false;
            else
                cropMode = false;
            break;
        case "brushSmaller": brush.step(-1); break;
        case "brushLarger": brush.step(1); break;
        case "export": openExport(); break;
        case "publish": openPublish(); break;
        default: console.warn("Unknown editor action", id);
        }
    }

    Component.onCompleted: photoChanged()

    Connections {
        target: App.editor
        function onCurrentChanged() {
            page.photoChanged();
        }
        function onViewChanged() {
            page.whitePicking = false;
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
    readonly property bool keysActive: App.page === "editor" && visible && !empty && !Keymap.recording
        && Dialogs.current === null && !exportDialog.visible && !publishDialog.visible && !_textFocus

    // As teclas vêm do Keymap (trocáveis no editor de atalhos); as condições
    // de cada uma ficam aqui.
    readonly property bool _photoKeys: keysActive && !selecting && !App.editor.picking && hasCurrent
    /// Teclas de GESTÃO (rescan, merge): só na visualização das fotos.
    readonly property bool _editKeys: keysActive && editable
    /// Teclas de REVELAÇÃO (auto, crop, níveis, seleção…): também no Instagram.
    readonly property bool _develKeys: keysActive && editing
    Shortcut {
        sequences: Keymap.keys("nextPhoto")
        enabled: page.keysActive && !page._arrowFocus && !page.selecting
        onActivated: page.run("nextPhoto")
    }
    Shortcut {
        sequences: Keymap.keys("previousPhoto")
        enabled: page.keysActive && !page._arrowFocus && !page.selecting
        onActivated: page.run("previousPhoto")
    }
    // Pintando, nada exclui nem arquiva: um Backspace perdido não pode jogar
    // a foto fora. Na lixeira nenhuma tecla apaga — excluir de vez é só pelos
    // botões, com confirmação digitada.
    Shortcut {
        sequences: Keymap.keys("deletePhoto")
        autoRepeat: false
        enabled: page._photoKeys && page.view !== "trash"
        onActivated: page.run("deletePhoto")
    }
    Shortcut {
        sequences: Keymap.keys("archivePhoto")
        autoRepeat: false
        enabled: page._photoKeys && page.editable
        onActivated: page.run("archivePhoto")
    }
    Shortcut {
        sequences: Keymap.keys("restorePhoto")
        autoRepeat: false
        enabled: page._photoKeys && !page.editable
        onActivated: page.run("restorePhoto")
    }
    Shortcut {
        sequences: Keymap.keys("purgePhoto")
        autoRepeat: false
        enabled: page._photoKeys && page.view === "trash"
        onActivated: page.run("purgePhoto")
    }
    Shortcut {
        sequences: Keymap.keys("emptyTrash")
        autoRepeat: false
        enabled: page.keysActive && page.view === "trash" && App.editor.trashCount > 0
        onActivated: page.run("emptyTrash")
    }
    Shortcut {
        sequences: Keymap.keys("showPhotos")
        autoRepeat: false
        enabled: page.keysActive && !page.selecting
        onActivated: page.run("showPhotos")
    }
    Shortcut {
        sequences: Keymap.keys("showInstagram")
        autoRepeat: false
        enabled: page.keysActive && !page.selecting
        onActivated: page.run("showInstagram")
    }
    Shortcut {
        sequences: Keymap.keys("showArchived")
        autoRepeat: false
        enabled: page.keysActive && !page.selecting
        onActivated: page.run("showArchived")
    }
    Shortcut {
        sequences: Keymap.keys("showTrash")
        autoRepeat: false
        enabled: page.keysActive && !page.selecting
        onActivated: page.run("showTrash")
    }
    Shortcut {
        sequences: Keymap.keys("duplicate")
        autoRepeat: false
        enabled: page._photoKeys && page.editable && page.current.isInstagram !== true
        onActivated: page.run("duplicate")
    }
    Shortcut {
        sequences: Keymap.keys("cover")
        autoRepeat: false
        enabled: page._photoKeys && page.editable && page.current.isInstagram !== true
        onActivated: page.run("cover")
    }
    Shortcut {
        sequences: Keymap.keys("instagram")
        autoRepeat: false
        enabled: page._photoKeys && (page.editable || page.view === "instagram")
        onActivated: page.run("instagram")
    }
    Shortcut {
        sequences: Keymap.keys("removeInstagram")
        autoRepeat: false
        enabled: page._photoKeys && (page.editable || page.view === "instagram") && page.current.isInstagram === true
        onActivated: page.run("removeInstagram")
    }
    Shortcut {
        sequences: Keymap.keys("rescan")
        autoRepeat: false
        enabled: page._editKeys && !page.selecting
        onActivated: page.run("rescan")
    }
    Shortcut {
        sequences: Keymap.keys("merge")
        autoRepeat: false
        enabled: page._editKeys && !page.selecting && App.editor.photos.count >= 2
        onActivated: page.run("merge")
    }
    Shortcut {
        sequences: Keymap.keys("undo")
        autoRepeat: false
        enabled: page.keysActive
        onActivated: page.run("undo")
    }
    Shortcut {
        sequences: Keymap.keys("redo")
        autoRepeat: false
        enabled: page.keysActive
        onActivated: page.run("redo")
    }
    Shortcut {
        sequences: Keymap.keys("auto")
        autoRepeat: false
        enabled: page._develKeys && !page.selecting && page.hasCurrent
        onActivated: page.run("auto")
    }
    Shortcut {
        sequences: Keymap.keys("reset")
        autoRepeat: false
        enabled: page._develKeys && !page.selecting && page.hasCurrent
        onActivated: page.run("reset")
    }
    Shortcut {
        sequences: Keymap.keys("crop")
        autoRepeat: false
        enabled: page._develKeys && !page.selecting && page.hasCurrent
        onActivated: page.run("crop")
    }
    Shortcut {
        sequences: Keymap.keys("nextLayer")
        autoRepeat: false
        enabled: page._develKeys && !page.selecting && !page.cropMode
        onActivated: page.run("nextLayer")
    }
    Shortcut {
        sequences: Keymap.keys("whitePicker")
        autoRepeat: false
        enabled: page._develKeys && !page.selecting && page.hasCurrent
        onActivated: page.run("whitePicker")
    }
    // Abas do painel de edição (Alt+1..4).
    Repeater {
        model: ["tabGeneral", "tabLevels", "tabCurves", "tabEffects"]
        delegate: Item {
            id: tabKey
            required property string modelData
            Shortcut {
                sequences: Keymap.keys(tabKey.modelData)
                autoRepeat: false
                enabled: page._develKeys && !page.selecting && Keymap.keys(tabKey.modelData).length > 0
                onActivated: page.run(tabKey.modelData)
            }
        }
    }
    Shortcut {
        sequences: Keymap.keys("selectArea")
        autoRepeat: false
        enabled: page._develKeys && page.hasCurrent
        onActivated: page.run("selectArea")
    }
    Shortcut {
        sequences: Keymap.keys("applySelection")
        autoRepeat: false
        enabled: page.keysActive && page.selecting && !page._buttonFocus
        onActivated: page.run("applySelection")
    }
    Shortcut {
        sequences: Keymap.keys("cancel")
        autoRepeat: false
        enabled: page.keysActive && (page.selecting || page.cropMode || page.whitePicking)
        onActivated: page.run("cancel")
    }
    Shortcut {
        sequences: Keymap.keys("brushSmaller")
        enabled: page.keysActive && page.selecting
        onActivated: page.run("brushSmaller")
    }
    Shortcut {
        sequences: Keymap.keys("brushLarger")
        enabled: page.keysActive && page.selecting
        onActivated: page.run("brushLarger")
    }

    // --------------------------------------------------------------------- //
    // Vazio: o catálogo inteiro (a tela toda) ou só a visualização atual
    // --------------------------------------------------------------------- //

    component EmptyState: ColumnLayout {
        id: emptyState

        property string iconName: ""
        property string title: ""
        property string hint: ""
        property bool showRescan: false

        spacing: 12

        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: emptyState.iconName
            size: 40
            color: Theme.mutedForeground
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: emptyState.title
            font.pixelSize: Theme.fontLg
            font.weight: Font.DemiBold
            color: Theme.foreground
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: emptyState.hint !== ""
            horizontalAlignment: Text.AlignHCenter
            text: emptyState.hint
            font.pixelSize: Theme.fontBase
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: emptyState.showRescan
            variant: "outline"
            iconName: "refresh-cw"
            text: qsTr("Rescan")
            onClicked: App.editor.rescan()
        }
    }

    readonly property var emptyContent: {
        if (view === "archived") {
            const key = Keymap.label("archivePhoto");
            return {
                icon: "archive",
                title: qsTr("No archived photos"),
                hint: key !== "" ? qsTr("Press %1 on a photo to archive it.").arg(key)
                                 : qsTr("Archive a photo to keep it out of the filmstrip and the export.")
            };
        }
        if (view === "trash")
            return {
                icon: "trash-2",
                title: qsTr("The trash is empty"),
                hint: qsTr("Photos moved to the trash wait here until you restore them or delete them permanently.")
            };
        if (view === "instagram") {
            const key = Keymap.label("instagram");
            return {
                icon: "instagram",
                title: qsTr("No Instagram versions"),
                hint: key !== "" ? qsTr("Press %1 on a photo to create its Instagram version.").arg(key)
                                 : qsTr("Create a photo's Instagram version to prepare it for the feed.")
            };
        }
        return {
            icon: "image-off",
            title: qsTr("No photos in the catalog"),
            hint: qsTr("Put the original JPEGs in the raw/ folder of the project and rescan.")
        };
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 640)
        visible: page.empty
        iconName: page.emptyContent.icon
        title: page.emptyContent.title
        hint: page.emptyContent.hint
        showRescan: true
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
                    maskUrl: page.selecting ? App.editor.selectionMaskUrl
                           : page.editing ? App.editor.activeLayerMaskUrl : ""
                    faint: !page.selecting
                    picking: page.whitePicking
                    pickHint: qsTr("Click a white or neutral area of the photo")
                    onPicked: (x, y) => page.pickWhite(x, y)
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
                            visible: page.editable && page.current.isMergeBase === true
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
                            visible: page.current.isInstagram === true && (page.editable || page.view === "instagram")
                            variant: "outline"
                            size: "sm"
                            iconName: "chevron-left"
                            text: qsTr("Back to the photo") + Keymap.hint("instagram")
                            onClicked: App.editor.toggleInstagram()
                        }
                        Button {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            visible: page.current.isInstagram === true && (page.editable || page.view === "instagram")
                            variant: "outline"
                            size: "sm"
                            iconName: "trash-2"
                            text: qsTr("Remove Instagram version")
                            onClicked: App.editor.removeInstagramVersion()
                        }

                        // Arquivada ou na lixeira: onde ela está e o caminho de volta.
                        Rectangle {
                            id: placeBanner

                            readonly property bool trash: page.view === "trash"

                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            visible: !page.editable && page.view !== "instagram"
                            implicitHeight: bannerColumn.implicitHeight + 24
                            radius: Theme.radiusMd
                            color: trash ? Theme.alpha(Theme.amber400, 0.12) : Theme.muted
                            border.color: trash ? Theme.alpha(Theme.amber400, 0.4) : Theme.border
                            Accessible.role: Accessible.AlertMessage
                            Accessible.name: bannerTitle.text + "\n" + bannerText.text

                            ColumnLayout {
                                id: bannerColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Icon {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 1
                                        name: placeBanner.trash ? "trash-2" : "archive"
                                        size: 16
                                        color: placeBanner.trash ? Theme.warning : Theme.foreground
                                    }
                                    Text {
                                        id: bannerTitle
                                        Layout.fillWidth: true
                                        text: placeBanner.trash ? qsTr("In the trash")
                                                                : qsTr("Archived photo — restore it to edit")
                                        font.pixelSize: Theme.fontBase
                                        font.weight: Font.DemiBold
                                        color: Theme.foreground
                                        wrapMode: Text.Wrap
                                    }
                                }
                                Text {
                                    id: bannerText
                                    Layout.fillWidth: true
                                    text: placeBanner.trash
                                        ? qsTr("It can still be restored. Deleting permanently cannot be undone.")
                                        : qsTr("Archived photos stay out of the filmstrip and the export. Their adjustments are kept.")
                                    font.pixelSize: Theme.fontSm
                                    color: Theme.mutedForeground
                                    wrapMode: Text.Wrap
                                }
                                Button {
                                    Layout.fillWidth: true
                                    variant: "outline"
                                    size: "sm"
                                    iconName: "archive-restore"
                                    text: qsTr("Restore") + Keymap.hint("restorePhoto")
                                    onClicked: App.editor.restoreCurrent()
                                }
                            }
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
                        id: editPanel
                        Layout.fillWidth: true
                        Layout.margins: 16
                        visible: !page.selecting && page.editing
                        disabled: !page.hasCurrent
                        cropMode: page.cropMode
                        whitePicking: page.whitePicking
                        onWhitePickToggled: page.setWhitePicking(!page.whitePicking)
                        onCropToggled: page.setCropMode(!page.cropMode)
                        onCropEdited: (crop, mode) => page.updateCrop(crop, mode)
                        onCropCommitted: App.editor.commitCrop()
                        onCropRestarted: page.baseScale = App.editor.crop.scale
                        onSelectRequested: (layerId) => page.startSelect(layerId)
                    }

                    Separator {
                        Layout.fillWidth: true
                        visible: page.editing
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

        // Visualização vazia (arquivadas, lixeira ou fotos com o resto em
        // outra visualização): cobre fotos e painel; a filmstrip e a barra
        // ficam, que é por ela que se troca de visualização.
        Rectangle {
            visible: page.listEmpty
            // Acima dos splitters (z 20) de fotos e painel; o da filmstrip
            // continua acessível logo abaixo.
            z: 21
            width: scroller.width
            height: page.mobile ? scroller.stripTop : scroller.topHeight
            color: Theme.background

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 640)
                iconName: page.emptyContent.icon
                title: page.emptyContent.title
                hint: page.emptyContent.hint
                showRescan: page.editable
            }
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
                onPublishRequested: publishDialog.open()
                onPurgeRequested: page.confirmPurge()
                onEmptyTrashRequested: page.confirmEmptyTrash()
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
