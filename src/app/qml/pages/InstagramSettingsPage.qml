pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PhotoEditor

// Configurações > Instagram (InstagramSettings.jsx): a conta para onde o
// "Publicar no Instagram" envia e o modelo da legenda.
//
// O login é o do app do celular (usuário e senha); o auxiliar guarda só a
// sessão em ~/.photoe, nunca a senha. Ele pode parar no meio pedindo um
// código — verificação em duas etapas ou o "confirme que é você" do
// Instagram — e então a tela troca o formulário pelo campo do código.
ScrollPage {
    id: page

    /// Volta para a tela de onde veio (Main).
    signal backRequested()

    readonly property var ig: App.instagram
    readonly property var login: ig.login || ({})
    readonly property string loginState: login.state || ""
    readonly property bool waitingCode: loginState === "code"
    readonly property bool pending: busy === "login" || busy === "code" || loginState === "running"

    // Ação em andamento: "" | "login" | "code" | "cancel" | "template".
    property string busy: ""
    // O erro do login só vale para um login feito nesta visita: o auxiliar
    // guarda o último até o próximo começar.
    property bool tried: false
    property bool templateLoaded: false
    property bool templateSaved: false

    spacing: 16

    Component.onCompleted: {
        page.ig.refresh();
        // Já há estado (visita anterior): usa o modelo dele de saída.
        templateArea.text = page.ig.captionTemplate;
    }

    Connections {
        target: page.ig

        function onStatusChanged() {
            if (page.busy === "template") {
                page.busy = "";
                page.templateSaved = true;
                templateArea.text = page.ig.captionTemplate;
            } else if (!page.templateLoaded) {
                templateArea.text = page.ig.captionTemplate;
            }
            page.templateLoaded = true;
            if (page.busy === "cancel")
                page.busy = "";
        }
        function onLoginChanged() {
            if (page.busy === "login" || page.busy === "code" || page.busy === "cancel") {
                page.busy = "";
                passwordField.text = "";
                codeField.text = "";
            }
        }
        // O erro em si vira o modal global (Main); aqui só destrava a tela.
        function onFailed(key, params) {
            page.busy = "";
        }
    }

    function connect() {
        if (!usernameField.text.trim() || !passwordField.text || pending)
            return;
        tried = true;
        busy = "login";
        ig.startLogin(usernameField.text.trim().replace(/^@/, ""), passwordField.text);
    }

    function sendCode() {
        if (!codeField.text.trim() || pending)
            return;
        busy = "code";
        ig.submitCode(codeField.text.trim());
    }

    function saveTemplate() {
        templateSaved = false;
        busy = "template";
        const text = templateArea.text;
        // Igual ao padrão = "sem modelo próprio": acompanha o padrão do app.
        ig.saveCaptionTemplate(text === ig.defaultTemplate ? "" : text);
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
            name: "instagram"
            size: 20
            color: Theme.brand400
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Instagram")
            font.pixelSize: Theme.fontLg
            font.weight: Font.DemiBold
            color: Theme.foreground
        }
    }

    FormError {
        Layout.fillWidth: true
        text: page.ig.available ? "" : Messages.error("instagram.unavailable")
    }

    // ------------------------------------------------------------------ //
    // Conta
    // ------------------------------------------------------------------ //
    Card {
        Layout.fillWidth: true
        title: qsTr("Account")
        description: qsTr("The account the photos are published to. The app keeps only the Instagram session on this computer — never the password.")
        enabled: page.ig.available

        // Conectado.
        Rectangle {
            Layout.fillWidth: true
            visible: page.ig.connected && !page.waitingCode
            implicitHeight: connectedRow.implicitHeight + 16
            radius: Theme.radiusMd
            color: "transparent"
            border.color: Theme.border

            RowLayout {
                id: connectedRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Icon {
                    name: "circle-check"
                    size: 16
                    color: Theme.success
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Connected as @%1.").arg(page.ig.username)
                    font.pixelSize: Theme.fontBase
                    color: Theme.foreground
                    elide: Text.ElideRight
                }
                Button {
                    variant: "outline"
                    size: "sm"
                    iconName: "log-out"
                    text: qsTr("Disconnect")
                    onClicked: Dialogs.confirm({
                        title: qsTr("Disconnect the account?"),
                        description: qsTr("The app forgets the session of @%1. Photos and posts already published are not touched.")
                            .arg(Messages.escapeHtml(page.ig.username)),
                        variant: "warning",
                        confirmLabel: qsTr("Disconnect"),
                        onConfirm: () => page.ig.logout()
                    })
                }
            }
        }

        // Pedindo código (2FA ou desafio).
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.waitingCode
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    name: "key-round"
                    size: 16
                    color: Theme.brand400
                }
                Text {
                    Layout.fillWidth: true
                    text: page.login.kind === "two_factor"
                          ? qsTr("Enter the code from your authenticator app or the SMS Instagram sent.")
                          : qsTr("Instagram sent a security code by e-mail or SMS to confirm this login. Enter it below.")
                    font.pixelSize: Theme.fontBase
                    color: Theme.foreground
                    wrapMode: Text.Wrap
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 6
                    FieldLabel {
                        text: qsTr("Code")
                        buddy: codeField
                    }
                    TextField {
                        id: codeField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        onAccepted: page.sendCode()
                        onVisibleChanged: if (visible) forceActiveFocus()
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                }
            }

            RowLayout {
                spacing: 8
                Button {
                    text: qsTr("Confirm")
                    loading: page.pending
                    disabled: !codeField.text.trim()
                    onClicked: page.sendCode()
                }
                Button {
                    variant: "ghost"
                    text: qsTr("Cancel")
                    disabled: page.busy === "cancel"
                    onClicked: {
                        page.busy = "cancel";
                        page.ig.cancelLogin();
                        page.ig.refresh();
                    }
                }
            }
        }

        // Formulário de login.
        ColumnLayout {
            Layout.fillWidth: true
            visible: !page.waitingCode
            spacing: 12

            Text {
                Layout.fillWidth: true
                visible: page.ig.connected
                text: qsTr("To use another account, connect it below.")
                font.pixelSize: Theme.fontBase
                color: Theme.mutedForeground
                wrapMode: Text.Wrap
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 6
                    FieldLabel {
                        text: qsTr("Username")
                        buddy: usernameField
                    }
                    TextField {
                        id: usernameField
                        Layout.fillWidth: true
                        placeholderText: "@"
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        onAccepted: passwordField.forceActiveFocus()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 6
                    FieldLabel {
                        text: qsTr("Password")
                        buddy: passwordField
                    }
                    TextField {
                        id: passwordField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        onAccepted: page.connect()
                    }
                }
            }

            Button {
                iconName: "instagram"
                text: page.pending ? qsTr("Connecting…") : qsTr("Connect")
                loading: page.pending
                disabled: !usernameField.text.trim() || !passwordField.text
                onClicked: page.connect()
            }
        }

        FormError {
            Layout.fillWidth: true
            items: page.tried && page.loginState === "error"
                   ? [Messages.error(page.login.error || "error.generic"), page.login.detail || ""]
                   : []
        }

        // Aviso: login fora da API oficial.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: warningRow.implicitHeight + 16
            radius: Theme.radiusMd
            color: Theme.alpha(Theme.muted, 0.4)
            border.color: Theme.border

            RowLayout {
                id: warningRow
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: "info"
                    size: 14
                    color: Theme.mutedForeground
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("The app signs in like the Instagram phone app, outside Meta's official API. Instagram may ask you to confirm the login or temporarily limit the account; publishing at a normal pace avoids that.")
                    font.pixelSize: Theme.fontSm
                    color: Theme.mutedForeground
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // ------------------------------------------------------------------ //
    // Modelo da legenda
    // ------------------------------------------------------------------ //
    Card {
        Layout.fillWidth: true
        title: qsTr("Caption template")
        description: qsTr("Fills the caption of each post with the event data. You can still edit it before publishing.")
        contentSpacing: 12
        enabled: page.ig.available

        TextArea {
            id: templateArea
            Layout.fillWidth: true
            rows: 6
            Accessible.name: qsTr("Caption template")
            onTextChanged: if (activeFocus) page.templateSaved = false
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 16
            rowSpacing: 4

            Repeater {
                model: [
                    { field: "event", text: qsTr("event name (the project folder, without the date)") },
                    { field: "championship", text: qsTr("event championship") },
                    { field: "category", text: qsTr("event category") },
                    { field: "date", text: qsTr("day or period of the photos") },
                    { field: "photos", text: qsTr("number of photos of the event") },
                    { field: "hashtag", text: qsTr("event name as a hashtag") }
                ]
                RowLayout {
                    id: legend
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 6

                    Rectangle {
                        implicitWidth: code.implicitWidth + 8
                        implicitHeight: code.implicitHeight + 2
                        radius: Theme.radiusSm
                        color: Theme.muted
                        Text {
                            id: code
                            anchors.centerIn: parent
                            text: "{" + legend.modelData.field + "}"
                            font.family: "monospace"
                            font.pixelSize: Theme.fontSm
                            color: Theme.foreground
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: legend.modelData.text
                        font.pixelSize: Theme.fontSm
                        color: Theme.mutedForeground
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        RowLayout {
            spacing: 12

            Button {
                text: qsTr("Save")
                loading: page.busy === "template"
                onClicked: page.saveTemplate()
            }
            Button {
                variant: "ghost"
                text: qsTr("Use the default")
                onClicked: {
                    templateArea.text = page.ig.defaultTemplate;
                    page.templateSaved = false;
                }
            }
            Row {
                visible: page.templateSaved
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
