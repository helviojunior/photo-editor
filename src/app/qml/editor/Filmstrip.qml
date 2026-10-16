pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import PhotoEditor

// Faixa de thumbnails (Filmstrip.jsx): a foto em edição fica SEMPRE ao
// centro, as anteriores à esquerda e as seguintes à direita. Os espaçadores
// de meia largura nas pontas deixam a primeira e a última chegarem ao centro.
//
// As versões Instagram estão no modelo (o editor navega até elas pelo I),
// mas não na faixa: a foto de origem leva o ícone e fica marcada enquanto a
// versão está aberta.
//
// No modo de marcar fotos (App.editor.picking, o merge) clicar marca/desmarca
// em vez de abrir, cada foto ganha o círculo de seleção (regra 2.3) e a
// primeira marcada leva o selo de base.
//
// Nas arquivadas e na lixeira (App.editor.view) cada foto aparece como está —
// inclusive uma versão Instagram, que ali não tem a origem para representá-la
// — e só com os selos de cópia e capa: nada que convide a editar.
ListView {
    id: strip

    readonly property var current: App.editor.current
    readonly property bool picking: App.editor.picking
    readonly property bool editable: App.editor.view === "photos"
    /// Linha da foto atual na faixa (na versão Instagram, a da origem).
    readonly property int stripIndex: {
        const c = current;
        if (!c || !c.id)
            return -1;
        if (c.isInstagram && editable)
            return App.editor.photos.indexOf(c.instagramOf);
        return App.editor.currentIndex;
    }

    function centerCurrent(animate) {
        if (stripIndex < 0 || count === 0 || width <= 0)
            return;
        const from = contentX;
        positionViewAtIndex(stripIndex, ListView.Center);
        const to = contentX;
        if (!animate || Math.abs(to - from) < 1) {
            scroll.stop();
            return;
        }
        contentX = from;
        scroll.from = from;
        scroll.to = to;
        scroll.restart();
    }

    orientation: ListView.Horizontal
    model: App.editor.photos
    clip: true
    spacing: 0
    boundsBehavior: Flickable.StopAtBounds
    reuseItems: true
    cacheBuffer: 1200

    header: Item {
        width: strip.width / 2
        height: 1
    }
    footer: Item {
        width: strip.width / 2
        height: 1
    }

    T.ScrollBar.horizontal: ThinScrollBar {}

    NumberAnimation {
        id: scroll
        target: strip
        property: "contentX"
        duration: 250
        easing.type: Easing.OutCubic
    }

    onStripIndexChanged: Qt.callLater(centerCurrent, true)
    onCountChanged: Qt.callLater(centerCurrent, false)
    onWidthChanged: Qt.callLater(centerCurrent, false)
    // A altura muda a largura dos thumbnails: centraliza de novo.
    onHeightChanged: Qt.callLater(centerCurrent, false)
    Component.onCompleted: Qt.callLater(centerCurrent, false)

    delegate: Item {
        id: cell

        required property int index
        required property string photoId
        required property string fileName
        required property real aspect
        required property string thumbUrl
        required property bool isCopy
        required property bool isInstagram
        required property bool hasInstagram
        required property bool isCover
        required property bool isMergeBase
        required property bool picked
        required property int pickOrder

        readonly property bool active: index === strip.stripIndex
        readonly property bool marked: strip.picking && picked
        readonly property real thumbHeight: Math.max(0, height - 16)
        readonly property real thumbWidth: thumbHeight / (aspect > 0 ? aspect : 2 / 3)

        readonly property bool hidden: isInstagram && strip.editable

        width: hidden ? 0 : thumbWidth + 8
        height: strip.height
        visible: !hidden

        Accessible.role: strip.picking ? Accessible.CheckBox : Accessible.Button
        Accessible.name: fileName
        Accessible.checkable: strip.picking
        Accessible.checked: marked

        // ring-2 ring-brand-400 ring-offset-2
        Rectangle {
            visible: cell.active || cell.marked
            x: thumb.x - 4
            y: thumb.y - 4
            width: thumb.width + 8
            height: thumb.height + 8
            radius: 6
            color: "transparent"
            border.width: 2
            border.color: Theme.brand400
        }

        Rectangle {
            id: thumb
            x: 4
            y: 8
            width: cell.thumbWidth
            height: cell.thumbHeight
            radius: 2
            color: Theme.neutral800
            clip: true
            opacity: cell.active || cell.marked || mouse.containsMouse ? 1 : 0.6
            Behavior on opacity { NumberAnimation { duration: Theme.animationFast } }

            Image {
                anchors.fill: parent
                // A versão Instagram não aparece na faixa: nem pede a imagem.
                source: cell.hidden ? "" : cell.thumbUrl
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                smooth: true
            }

            SelectionCheck {
                visible: strip.picking
                x: 6
                y: 6
                checked: cell.marked
                color: checked ? Theme.primary : Theme.alpha(Theme.black, 0.5)
                border.color: checked ? Theme.primary : Theme.white
            }

            // MERGE / COPIA (embaixo, à esquerda)
            Rectangle {
                visible: !strip.picking && ((strip.editable && cell.isMergeBase) || cell.isCopy)
                x: 4
                y: parent.height - height - 4
                width: kindLabel.implicitWidth + 12
                height: kindLabel.implicitHeight + 4
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.black, 0.7)
                Text {
                    id: kindLabel
                    anchors.centerIn: parent
                    text: strip.editable && cell.isMergeBase ? qsTr("Merge") : qsTr("Copy")
                    font.pixelSize: Theme.fontTiny
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    color: Theme.white
                }
            }

            // Tem versão Instagram (em cima, à esquerda)
            Rectangle {
                id: instagramBadge
                visible: !strip.picking && strip.editable && cell.hasInstagram
                x: 4
                y: 4
                width: 16
                height: 16
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.black, 0.7)
                Icon {
                    anchors.centerIn: parent
                    name: "instagram"
                    size: 12
                    color: Theme.white
                }
                HoverHandler { id: instagramHover }
                Tip {
                    text: qsTr("Has an Instagram version")
                    visible: instagramHover.hovered
                }
            }

            // Capa (em cima, à direita)
            Rectangle {
                visible: cell.isCover
                x: parent.width - width - 4
                y: 4
                width: coverRow.implicitWidth + 12
                height: coverRow.implicitHeight + 4
                radius: Theme.radiusSm
                color: Theme.brand500
                Row {
                    id: coverRow
                    anchors.centerIn: parent
                    spacing: 2
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        sourceSize: Qt.size(Math.ceil(10 * Screen.devicePixelRatio), Math.ceil(10 * Screen.devicePixelRatio))
                        source: Theme.iconUrl("star", Theme.white) + "&fill=1"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Cover")
                        font.pixelSize: Theme.fontTiny
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        color: Theme.white
                    }
                }
            }

            // Base do merge (a primeira marcada)
            Rectangle {
                visible: strip.picking && cell.picked && cell.pickOrder === 0
                x: 4
                y: parent.height - height - 4
                width: baseLabel.implicitWidth + 12
                height: baseLabel.implicitHeight + 4
                radius: Theme.radiusSm
                color: Theme.brand500
                Text {
                    id: baseLabel
                    anchors.centerIn: parent
                    text: qsTr("Base")
                    font.pixelSize: Theme.fontTiny
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    color: Theme.white
                }
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: thumb
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (strip.picking)
                    App.editor.togglePick(cell.photoId);
                else
                    App.editor.currentIndex = cell.index;
            }
        }

        Tip {
            text: cell.fileName
            visible: mouse.containsMouse && !instagramHover.hovered
        }
    }
}
