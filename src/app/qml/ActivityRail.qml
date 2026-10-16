pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import PhotoEditor

// Trilho vertical à esquerda (o ActivityRail do clivcut), em todas as telas.
// Não há barra de menus: Projetos e Sobre abrem os menus que eram Arquivo e
// Ajuda/Exibir.
//
//   Projetos   menu: Início, novo, abrir, recentes, mostrar pasta, fechar,
//              Exportar, Publicar, idioma, sair
//   Editor     volta ao editor do projeto aberto (do merge, fecha o merge;
//              desligado sem projeto)
//   Idioma     menu com App.languages; o atual com ✓
//   Instagram  conta do "Publicar"
//   Sobre      menu: Sobre, pasta de logs, tela cheia
//   (embaixo)  atalhos de teclado e tema claro/escuro
//
// O item da tela atual leva a barra e o fundo suave no acento da marca.
// As ações que abrem diálogos saem por sinal para o Main.
Rectangle {
    id: rail

    /// Fluxos de abrir/criar projeto (ProjectFlows do Main).
    required property var flows

    signal editorRequested()
    signal aboutRequested()
    signal shortcutsRequested()
    signal fullScreenRequested()
    signal exportRequested()
    signal publishRequested()
    signal quitRequested()

    // Só o que é fixo de cada item: estado (ativo, habilitado) é calculado no
    // delegate — mudar o modelo recriaria os botões (e o menu de idioma
    // perderia o botão em que está ancorado).
    readonly property var items: [
        { key: "home", icon: "folder", label: qsTr("Projects") },
        { key: "editor", icon: "sliders-horizontal", label: qsTr("Editor") },
        { key: "language", icon: "languages", label: qsTr("Language") },
        { key: "instagram", icon: "instagram", label: qsTr("Instagram") },
        { key: "about", icon: "info", label: qsTr("About") }
    ]

    function isActive(key) {
        switch (key) {
        case "home": return App.page === "home" || projectsMenu.visible;
        case "editor": return App.page === "editor" || App.page === "merge";
        case "language": return languageMenu.visible;
        case "instagram": return App.page === "instagram";
        case "about": return aboutMenu.visible;
        }
        return false;
    }

    function activate(key, item) {
        switch (key) {
        case "home": projectsMenu.popup(item, item.width + 4, 0); break;
        case "editor": rail.editorRequested(); break;
        case "language": languageMenu.popup(item, item.width + 4, 0); break;
        case "instagram": App.page = "instagram"; break;
        case "about": aboutMenu.popup(item, item.width + 4, 0); break;
        }
    }

    implicitWidth: 56 + 1
    color: Theme.card

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 2

        Repeater {
            model: rail.items

            RailItem {
                id: entry
                required property var modelData
                Layout.alignment: Qt.AlignHCenter
                iconName: modelData.icon
                label: modelData.label
                shortcut: modelData.key === "home" ? Keymap.label("home") : ""
                active: rail.isActive(modelData.key)
                enabled: modelData.key !== "editor" || App.hasProject
                onClicked: rail.activate(modelData.key, entry)
            }
        }

        Item { Layout.fillHeight: true }

        IconButton {
            Layout.alignment: Qt.AlignHCenter
            iconName: "keyboard"
            tooltip: qsTr("Keyboard shortcuts")
            shortcut: Keymap.label("shortcuts")
            tooltipSide: "right"
            onClicked: rail.shortcutsRequested()
        }
        IconButton {
            Layout.alignment: Qt.AlignHCenter
            iconName: Theme.dark ? "sun" : "moon"
            tooltip: Theme.dark ? qsTr("Light theme") : qsTr("Dark theme")
            shortcut: Keymap.label("theme")
            tooltipSide: "right"
            onClicked: App.darkTheme = !App.darkTheme
        }
    }

    // Projetos: o que era o menu Arquivo (não há barra de menus), no modelo
    // do menu do projeto do clivcut. As teclas são só mostradas; quem as
    // dispara são os Shortcut globais do Main.
    AppMenu {
        id: projectsMenu

        AppMenuItem {
            text: qsTr("Home")
            shortcutText: Keymap.label("home")
            onTriggered: App.page = "home"
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("New project…")
            shortcutText: Keymap.label("newProject")
            onTriggered: rail.flows.newProject()
        }
        AppMenuItem {
            text: qsTr("Open project folder…")
            shortcutText: Keymap.label("openProject")
            onTriggered: rail.flows.openDialog()
        }
        AppMenu {
            id: recentMenu
            title: qsTr("Open recent")

            // Até 15 recentes; sem nenhum, um item apagado que avisa.
            Instantiator {
                model: App.recentProjects.length > 0 ? App.recentProjects.slice(0, 15) : [null]
                delegate: AppMenuItem {
                    id: recentItem
                    required property var modelData
                    text: recentItem.modelData ? recentItem.modelData.name : qsTr("No recent projects")
                    enabled: recentItem.modelData !== null
                    onTriggered: rail.flows.openRecent(recentItem.modelData)
                }
                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }
        }
        AppMenuItem {
            text: qsTr("Show project folder")
            enabled: App.hasProject
            onTriggered: App.revealInFolder(App.projectPath)
        }
        AppMenuItem {
            text: qsTr("Close project")
            shortcutText: Keymap.label("closeProject")
            enabled: App.hasProject
            onTriggered: App.closeProject()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Export…")
            shortcutText: Keymap.label("export")
            enabled: App.hasProject
            onTriggered: rail.exportRequested()
        }
        AppMenuItem {
            text: qsTr("Publish on Instagram…")
            shortcutText: Keymap.label("publish")
            enabled: App.hasProject && App.editor.hasInstagramVersions
            onTriggered: rail.publishRequested()
        }
        AppMenuSeparator {}
        LanguageMenu {
            title: qsTr("Language")
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Quit")
            shortcutText: Keymap.label("quit")
            onTriggered: rail.quitRequested()
        }
    }

    // Sobre: o que era o menu Ajuda, mais a tela cheia (o antigo Exibir).
    AppMenu {
        id: aboutMenu

        AppMenuItem {
            text: qsTr("About %1").arg(App.brandName)
            onTriggered: rail.aboutRequested()
        }
        AppMenuItem {
            text: qsTr("Open logs folder")
            onTriggered: App.openLogsFolder()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Toggle full screen")
            shortcutText: Keymap.label("fullScreen")
            onTriggered: rail.fullScreenRequested()
        }
    }

    // Idioma (item do trilho e submenu de Projetos): a escolha é salva pelo
    // C++ (QSettings) e aplicada na hora.
    LanguageMenu {
        id: languageMenu
    }

    component LanguageMenu: AppMenu {
        id: menu

        Instantiator {
            model: App.languages
            delegate: AppMenuItem {
                id: languageItem
                required property var modelData
                text: languageItem.modelData.name
                checkable: true
                checked: App.language === languageItem.modelData.code
                onTriggered: App.setLanguage(languageItem.modelData.code)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }

    // Um item do trilho: ícone + rótulo pequeno; ativo = barra à esquerda e
    // fundo suave no acento. Dica à direita com o atalho, quando há.
    component RailItem: QQC.AbstractButton {
        id: button

        property string iconName: ""
        property string label: ""
        property string shortcut: ""
        property bool active: false

        implicitWidth: 52
        implicitHeight: 52
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        opacity: enabled ? 1 : 0.4
        Accessible.name: label

        background: Item {
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: -2
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: button.active ? 26 : 0
                radius: 2
                color: Theme.brand500
                Behavior on height { NumberAnimation { duration: 120 } }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 46
                height: 46
                radius: Theme.radiusLg + 2
                color: button.active ? Theme.brandSoft : (button.hovered || button.down ? Theme.muted : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.animationFast } }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 48
                height: 48
                radius: Theme.radiusLg + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: button.visualFocus
            }
        }

        contentItem: Item {
            Column {
                anchors.centerIn: parent
                spacing: 3

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: button.iconName
                    size: 19
                    color: button.active ? Theme.brandForeground
                         : button.hovered ? Theme.foreground : Theme.mutedForeground
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, 50)
                    horizontalAlignment: Text.AlignHCenter
                    text: button.label
                    font.pixelSize: Theme.fontTiny - 1
                    font.weight: button.active ? Font.DemiBold : Font.Normal
                    color: button.active ? Theme.foreground : Theme.mutedForeground
                    elide: Text.ElideRight
                }
            }
        }

        Tip {
            text: button.shortcut !== "" ? button.label + "   " + button.shortcut : button.label
            visible: button.hovered
            x: button.width + 6
            y: Math.round((button.height - implicitHeight) / 2)
        }
    }
}
