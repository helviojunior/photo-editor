pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor
import "caption.js" as Caption

// "Publicar no Instagram" (components/instagram/InstagramPublishDialog.jsx):
// escolhe as versões Instagram da filmstrip (até App.instagram.maxPhotos),
// edita a legenda — já preenchida com os dados do evento (caption.js) — e
// publica. Quem envia é o InstagramController, que antes exporta cada versão
// para publicar/instagram/: sai exatamente o arquivo da pasta.
//
//   InstagramPublishDialog { id: publishDialog }
//   publishDialog.open()
//
// Ao abrir: pede o estado da conta (App.instagram.refresh()), descarta o
// resultado de um envio anterior já terminado e, na primeira vez, marca as
// primeiras versões publicáveis e preenche a legenda. Seleção e legenda
// ficam entre uma abertura e outra.
//
// Fechar durante o envio não o cancela: ele segue no C++ e, ao reabrir, o
// modal volta a mostrar o andamento.
Modal {
    id: dialog

    readonly property var ig: App.instagram
    // Até maxPhotos por post (carrossel); acima disso vão vários posts.
    readonly property int maxPhotos: ig.maxPhotos || 10
    readonly property int maxPosts: ig.maxPosts || 10
    readonly property int maxTotal: maxPhotos * maxPosts
    readonly property int captionMax: ig.captionMax || 2200
    readonly property var candidates: ig.candidates || []
    readonly property var pub: ig.publish || ({})

    /// Ids marcados (null = ainda não inicializado).
    property var selected: null
    property bool _statusSeen: false
    property bool _captionFilled: false

    readonly property bool available: ig.available
    readonly property bool loaded: _statusSeen || ig.connected
    readonly property bool running: !!pub.running
    readonly property bool published: !running && pub.url !== undefined && !pub.error
    readonly property bool failed: !running && !!pub.error
    readonly property bool needsAccount: available && loaded && !ig.connected && !running && !published

    // Marcadas e ainda publicáveis, na ordem da filmstrip (é a do carrossel).
    readonly property var picked: candidates.filter((c) => (selected || []).includes(c.id) && c.feedOk)
    readonly property var pickedIds: picked.map((c) => c.id)
    // Quantos posts sairão (lotes de até maxPhotos).
    readonly property int postCount: Math.max(1, Math.ceil(picked.length / maxPhotos))
    // O Instagram corta o carrossel inteiro na proporção da PRIMEIRA foto.
    readonly property bool mixedRatios: {
        const seen = {};
        for (const c of picked)
            seen[String(c.cropRatio)] = true;
        return Object.keys(seen).length > 1;
    }
    readonly property int captionLength: captionField.text.length
    readonly property bool tooLong: captionLength > captionMax

    function refill() {
        captionField.text = Caption.fillCaption(ig.captionTemplate, ig.eventInfo, App.language);
        _captionFilled = true;
    }

    function toggle(item) {
        if (running || !item.feedOk)
            return;
        const cur = (selected || []).filter((id) => candidates.some((c) => c.id === id && c.feedOk));
        if (cur.includes(item.id))
            selected = cur.filter((id) => id !== item.id);
        else if (cur.length < maxTotal)
            selected = cur.concat([item.id]);
    }

    function publishNow() {
        if (!pickedIds.length || tooLong || running)
            return;
        ig.startPublish(pickedIds, captionField.text);
    }

    function goToSettings() {
        close();
        App.page = "instagram";
    }

    onAboutToShow: {
        if (!pub.running)
            ig.resetPublish();
        _statusSeen = false;
        if (selected === null)
            selected = candidates.filter((c) => c.feedOk).slice(0, maxTotal).map((c) => c.id);
        if (!_captionFilled && ig.connected)
            refill();
        ig.refresh();
    }

    Connections {
        target: dialog.ig
        function onStatusChanged() {
            if (!dialog.visible)
                return;
            dialog._statusSeen = true;
            if (!dialog._captionFilled)
                dialog.refill();
        }
    }

    size: "xl"
    iconName: "instagram"
    variant: "brand"
    title: qsTr("Publish on Instagram")
    textAlignment: Text.AlignHCenter

    // ------------------------------------------------------------------ //
    // Carregando / indisponível
    // ------------------------------------------------------------------ //

    FormError {
        Layout.fillWidth: true
        text: dialog.available ? "" : Messages.error("instagram.unavailable")
    }

    Text {
        Layout.fillWidth: true
        visible: dialog.available && !dialog.loaded
        text: qsTr("Loading…")
        font.pixelSize: Theme.fontBase
        color: Theme.mutedForeground
    }

    // ------------------------------------------------------------------ //
    // Publicado
    // ------------------------------------------------------------------ //

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 8
        Layout.bottomMargin: 8
        visible: dialog.published
        spacing: 12

        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: "circle-check"
            size: 40
            color: Theme.success
        }
        Text {
            Layout.fillWidth: true
            readonly property var urls: dialog.pub.urls || (dialog.pub.url ? [dialog.pub.url] : [])
            text: urls.length > 1 ? qsTr("Published %n post(s) on Instagram.", "", urls.length)
                                  : qsTr("Published on Instagram.")
            font.pixelSize: Theme.fontBase
            font.weight: Font.Medium
            color: Theme.foreground
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        // Um link por post publicado.
        Column {
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Repeater {
                model: dialog.pub.urls || (dialog.pub.url ? [dialog.pub.url] : [])
                LinkButton {
                    required property int index
                    required property var modelData
                    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                    iconName: "external-link"
                    fontSize: Theme.fontBase
                    text: (dialog.pub.urls && dialog.pub.urls.length > 1)
                          ? qsTr("Open post %1").arg(index + 1)
                          : qsTr("Open the post")
                    onClicked: App.openUrl(modelData)
                }
            }
        }
    }

    // ------------------------------------------------------------------ //
    // Sem conta conectada
    // ------------------------------------------------------------------ //

    Rectangle {
        Layout.fillWidth: true
        visible: dialog.needsAccount
        implicitHeight: noAccountRow.implicitHeight + 16
        radius: Theme.radiusMd
        color: Theme.alpha(Theme.muted, 0.4)
        border.color: Theme.border

        RowLayout {
            id: noAccountRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8
            Icon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                name: "info"
                size: 14
                color: Theme.foreground
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("No Instagram account is connected to the app yet. Connect one to publish.")
                font.pixelSize: Theme.fontSm
                color: Theme.foreground
                wrapMode: Text.Wrap
            }
        }
    }

    // ------------------------------------------------------------------ //
    // Formulário
    // ------------------------------------------------------------------ //

    ColumnLayout {
        id: form
        Layout.fillWidth: true
        visible: dialog.available && dialog.loaded && !dialog.published && !dialog.needsAccount
        spacing: 16

        // "Publicando como @fulano. Trocar"
        Flow {
            Layout.fillWidth: true
            visible: !!dialog.ig.username
            spacing: 4
            Text {
                text: qsTr("Publishing as @%1.").arg(dialog.ig.username)
                font.pixelSize: Theme.fontSm
                color: Theme.mutedForeground
            }
            LinkButton {
                text: qsTr("Change")
                onClicked: dialog.goToSettings()
            }
        }

        // Fotos
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Photos")
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.Medium
                    color: Theme.foreground
                }
                Text {
                    text: dialog.postCount > 1
                          ? qsTr("%1 photos · %2 posts").arg(dialog.picked.length).arg(dialog.postCount)
                          : qsTr("%1 of %2").arg(dialog.picked.length).arg(dialog.maxPhotos)
                    font.pixelSize: Theme.fontSm
                    font.features: { "tnum": 1 }
                    color: Theme.mutedForeground
                }
            }

            // Mais de maxPhotos: Instagram só aceita 10 por post, então o envio
            // vira vários posts (carrosséis de 10) com a legenda numerada.
            RowLayout {
                Layout.fillWidth: true
                visible: dialog.postCount > 1
                spacing: 6
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: "info"
                    size: 12
                    color: Theme.mutedForeground
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("More than %1 photos: they go out as %2 separate posts (carousels of up to %1), each caption numbered “Part i/%2”.")
                          .arg(dialog.maxPhotos).arg(dialog.postCount)
                    font.pixelSize: Theme.fontXs
                    color: Theme.mutedForeground
                    wrapMode: Text.Wrap
                }
            }

            Text {
                Layout.fillWidth: true
                visible: dialog.candidates.length === 0
                text: qsTr("No photo has an Instagram version. Press I on a photo to create one.")
                font.pixelSize: Theme.fontSm
                color: Theme.mutedForeground
                wrapMode: Text.Wrap
            }

            Grid {
                id: grid
                Layout.fillWidth: true
                visible: dialog.candidates.length > 0
                // grid-cols-3 sm:grid-cols-5 (sm = janela a partir de 640px).
                columns: (dialog.parent && dialog.parent.width >= 640) ? 5 : 3
                spacing: 8
                readonly property real tile: Math.floor((width - spacing * (columns - 1)) / columns)

                Repeater {
                    model: dialog.candidates

                    Item {
                        id: tile
                        required property var modelData
                        readonly property bool checked: dialog.pickedIds.includes(modelData.id)
                        readonly property int order: dialog.pickedIds.indexOf(modelData.id)
                        readonly property bool outside: !modelData.feedOk
                        readonly property bool full: !checked && (dialog.picked.length >= dialog.maxTotal || outside)
                        readonly property bool blocked: full || dialog.running

                        width: grid.tile
                        height: grid.tile
                        activeFocusOnTab: true
                        opacity: checked || mouse.containsMouse ? 1 : 0.6
                        Behavior on opacity { NumberAnimation { duration: Theme.animationFast } }

                        Accessible.role: Accessible.CheckBox
                        Accessible.name: modelData.fileName
                        Accessible.checkable: true
                        Accessible.checked: checked
                        Accessible.onToggleAction: dialog.toggle(modelData)
                        Accessible.onPressAction: dialog.toggle(modelData)
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                event.accepted = true;
                                dialog.toggle(tile.modelData);
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusMd
                            color: Theme.neutral800
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: tile.modelData.thumbUrl || ""
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                                smooth: true
                                sourceSize.width: Math.ceil(grid.tile * Screen.devicePixelRatio)
                                sourceSize.height: Math.ceil(grid.tile * Screen.devicePixelRatio)
                            }
                        }

                        // ring-2 ring-brand-400 da marcada
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusMd
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.brand400
                            visible: tile.checked
                        }
                        // Anel de foco do teclado
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            radius: Theme.radiusMd + 3
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.focusRing
                            visible: tile.activeFocus
                        }

                        Rectangle {
                            x: 6
                            y: 6
                            width: 20
                            height: 20
                            radius: 10
                            color: Theme.alpha(Theme.black, 0.5)
                            SelectionCheck {
                                anchors.fill: parent
                                checked: tile.checked
                                border.color: tile.checked ? Theme.primary : Theme.alpha(Theme.white, 0.8)
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 4
                            visible: tile.outside
                            implicitHeight: outsideText.implicitHeight + 4
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.black, 0.75)
                            Text {
                                id: outsideText
                                anchors.centerIn: parent
                                width: parent.width - 8
                                text: qsTr("Vertical 1:1.91 — outside the feed")
                                font.pixelSize: Theme.fontTiny
                                color: Theme.white
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                lineHeight: 0.95
                            }
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 4
                            visible: tile.checked
                            width: orderText.implicitWidth + 12
                            height: orderText.implicitHeight + 2
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.black, 0.7)
                            Text {
                                id: orderText
                                anchors.centerIn: parent
                                text: tile.order + 1
                                font.pixelSize: Theme.fontTiny
                                font.weight: Font.DemiBold
                                font.features: { "tnum": 1 }
                                color: Theme.white
                            }
                        }

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: tile.blocked ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                            onClicked: {
                                tile.forceActiveFocus(Qt.MouseFocusReason);
                                dialog.toggle(tile.modelData);
                            }
                        }

                        Tip {
                            visible: mouse.containsMouse
                            text: tile.modelData.fileName
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: dialog.mixedRatios
                spacing: 6
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: "info"
                    size: 12
                    color: Theme.mutedForeground
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("The photos have different formats: Instagram crops the whole carousel to the format of the first one.")
                    font.pixelSize: Theme.fontXs
                    color: Theme.mutedForeground
                    wrapMode: Text.Wrap
                }
            }
        }

        // Legenda
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Caption")
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.Medium
                    color: Theme.foreground
                }
                LinkButton {
                    iconName: "rotate-ccw"
                    text: qsTr("Fill from the event")
                    muted: true
                    disabled: dialog.running
                    onClicked: dialog.refill()
                }
            }

            TextArea {
                id: captionField
                Layout.fillWidth: true
                rows: 7
                enabled: !dialog.running
                error: dialog.tooLong
                Accessible.name: qsTr("Caption")
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: -4
                text: dialog.captionLength + "/" + dialog.captionMax
                font.pixelSize: Theme.fontXs
                font.features: { "tnum": 1 }
                color: dialog.tooLong ? Theme.foreground : Theme.mutedForeground
                horizontalAlignment: Text.AlignRight
            }

            FormError {
                Layout.fillWidth: true
                text: dialog.tooLong ? Messages.error("instagram.captionTooLong", { max: dialog.captionMax }) : ""
            }
        }

        // Andamento do envio
        ColumnLayout {
            Layout.fillWidth: true
            visible: dialog.running
            spacing: 4

            Text {
                Layout.fillWidth: true
                text: {
                    if (dialog.pub.phase !== "upload")
                        return qsTr("Preparing the photos… %1/%2").arg(dialog.pub.done || 0).arg(dialog.pub.total || 0);
                    const posts = dialog.pub.posts || 1;
                    return posts > 1
                           ? qsTr("Sending post %1 of %2 to Instagram…").arg(dialog.pub.post || 1).arg(posts)
                           : qsTr("Sending to Instagram…");
                }
                font.pixelSize: Theme.fontSm
                color: Theme.mutedForeground
                Accessible.role: Accessible.StaticText
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 8
                radius: 4
                color: Theme.muted
                clip: true
                Rectangle {
                    height: parent.height
                    radius: 4
                    color: Theme.brand500
                    width: parent.width * (dialog.pub.phase === "upload"
                                           ? (dialog.pub.post || 1) / (dialog.pub.posts || 1)
                                           : Math.round((dialog.pub.done || 0) / (dialog.pub.total || 1) * 90) / 100)
                    Behavior on width { NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
                }
            }
        }

        FormError {
            Layout.fillWidth: true
            items: dialog.failed
                   ? [Messages.error(dialog.pub.error, dialog.pub.params || {}),
                      dialog.pub.detail || (dialog.pub.params || {}).detail || ""]
                   : []
        }

        // Falhou depois de já publicar alguns posts: diz quantos saíram.
        Text {
            Layout.fillWidth: true
            visible: dialog.failed && (dialog.pub.doneCount || 0) > 0
            text: qsTr("%n post(s) were published before the error.", "", dialog.pub.doneCount || 0)
            font.pixelSize: Theme.fontXs
            color: Theme.mutedForeground
            wrapMode: Text.Wrap
        }
    }

    footer: [
        Button {
            visible: dialog.published
            text: qsTr("Close")
            onClicked: dialog.close()
        },
        Button {
            visible: !dialog.published
            variant: "outline"
            text: dialog.running ? qsTr("Keep running in background") : qsTr("Cancel")
            onClicked: dialog.close()
        },
        Button {
            visible: dialog.needsAccount
            iconName: "instagram"
            text: qsTr("Connect account")
            onClicked: dialog.goToSettings()
        },
        Button {
            visible: !dialog.published && !dialog.needsAccount
            iconName: "instagram"
            text: qsTr("Publish")
            loading: dialog.running
            disabled: !dialog.available || !dialog.loaded || !dialog.pickedIds.length || dialog.tooLong
            onClicked: dialog.publishNow()
        }
    ]

    // Botão-texto (link): "Trocar", "Preencher com o evento", "Abrir o post".
    component LinkButton: Item {
        id: link
        property string text: ""
        property string iconName: ""
        /// Cinza que acende no hover (em vez do vermelho de acento).
        property bool muted: false
        property bool disabled: false
        property int fontSize: Theme.fontSm
        signal clicked()

        readonly property bool _hot: linkMouse.containsMouse && !disabled
        readonly property color _color: muted ? (_hot ? Theme.foreground : Theme.mutedForeground) : Theme.brand400

        implicitWidth: linkRow.implicitWidth
        implicitHeight: linkRow.implicitHeight
        opacity: disabled ? 0.5 : 1
        activeFocusOnTab: !disabled
        Accessible.role: Accessible.Link
        Accessible.name: text
        Accessible.onPressAction: if (!disabled) link.clicked()
        Keys.onPressed: (event) => {
            if (!link.disabled && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                event.accepted = true;
                link.clicked();
            }
        }

        Row {
            id: linkRow
            spacing: 4
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: link.iconName !== ""
                name: link.iconName
                size: link.fontSize > Theme.fontSm ? 16 : 12
                color: link._color
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: link.text
                font.pixelSize: link.fontSize
                font.underline: link._hot && !link.muted || link.activeFocus
                color: link._color
            }
        }
        MouseArea {
            id: linkMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: !link.disabled
            cursorShape: Qt.PointingHandCursor
            onClicked: link.clicked()
        }
    }
}
