import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import PhotoEditor

// Cabeçalho do app (AppLayout.jsx): logo + versão, Início e nome do projeto
// quando há um aberto; à direita o idioma, a conta do Instagram e o tema.
// Baixo de propósito: cada pixel que ele não usa vai para as fotos.
Rectangle {
    id: header

    signal instagramRequested()

    implicitHeight: Theme.headerHeight + 1
    color: Theme.card

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: 1
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        // Logo com o brilho branco (drop-shadow 0 0 10px white/40).
        Image {
            id: logo
            Layout.preferredHeight: 24
            Layout.preferredWidth: 24 * 2594 / 575
            source: Theme.dark ? "qrc:/qt/qml/PhotoEditor/assets/logo/photoe-dark.png"
                               : "qrc:/qt/qml/PhotoEditor/assets/logo/photoe-light.png"
            sourceSize.height: 24 * Screen.devicePixelRatio
            fillMode: Image.PreserveAspectFit
            smooth: true
            // Sem GPU (renderização por software) não há efeito: o logo vai sem brilho.
            layer.enabled: Theme.dark && GraphicsInfo.api !== GraphicsInfo.Software
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Qt.rgba(1, 1, 1, 0.4)
                shadowBlur: 0.5
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
                blurMax: 20
                autoPaddingEnabled: true
            }
        }

        Text {
            text: "v" + App.version
            font.pixelSize: Theme.fontXs
            color: Theme.mutedForeground
        }

        // Projeto aberto: Início + nome (clicar no nome volta ao editor).
        Separator {
            vertical: true
            visible: App.hasProject
            Layout.leftMargin: 4
            Layout.rightMargin: 4
        }
        IconButton {
            visible: App.hasProject
            iconName: "house"
            tooltip: qsTr("Home")
            active: App.page === "home"
            onClicked: App.page = "home"
        }
        Text {
            id: projectName
            visible: App.hasProject
            Layout.maximumWidth: header.width * 0.4
            text: App.projectName
            font.pixelSize: Theme.fontBase
            font.weight: Font.Medium
            color: Theme.foreground
            elide: Text.ElideRight

            MouseArea {
                id: nameMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: App.page !== "editor" ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (App.page !== "editor" && App.page !== "merge")
                        App.page = "editor";
                }
            }
            Tip {
                text: App.projectPath
                visible: nameMouse.containsMouse
            }
        }

        Item { Layout.fillWidth: true }

        // Idioma: escolha salva pelo C++ (QSettings) e aplicada na hora.
        Select {
            id: language
            variant: "ghost"
            model: App.languages
            textRole: "name"
            valueRole: "code"
            currentIndex: App.languages.findIndex((l) => l.code === App.language)
            Accessible.name: qsTr("Language")
            onActivated: App.setLanguage(currentValue)
        }

        Separator {
            vertical: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
        }

        // Conta do Instagram para o "Publicar" (vale para todos os projetos).
        IconButton {
            iconName: "instagram"
            tooltip: qsTr("Instagram settings")
            active: App.page === "instagram"
            onClicked: header.instagramRequested()
        }

        IconButton {
            iconName: Theme.dark ? "sun" : "moon"
            tooltip: qsTr("Theme")
            onClicked: App.darkTheme = !App.darkTheme
        }
    }
}
