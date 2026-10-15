import QtQuick
import QtQuick.Dialogs
import QtCore
import PhotoEditor

// Abrir e criar projeto — os fluxos que eram do shell desktop
// (window.py: choose_project, open_project, new_project, _adopt_photos,
// _copy_photos). Escolher pasta/arquivos usa o diálogo do SO (QtQuick.Dialogs);
// toda pergunta e aviso no meio do caminho é modal do app (Dialogs).
//
// Instanciado uma vez no Main.qml e usado pela Home e pelo menu Arquivo:
//
//   flows.openDialog()          Arquivo > Abrir Pasta de Projeto…
//   flows.newProject()          Arquivo > Novo Projeto…
//   flows.openFolder(url)       abre uma pasta (pergunta pelo raw/ se faltar)
//   flows.openRecent(project)   card da Home / Abrir Recente (item de App.recentProjects)
//   flows.removeRecent(project) tira da lista, com confirmação
Item {
    id: flows

    visible: false

    // Projeto aguardando a cópia de fotos terminar (importPhotos).
    property string _importing: ""

    readonly property url homeFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)

    /// Caminho local -> file:// URL (com os caracteres especiais escapados).
    function toUrl(path) {
        const text = String(path || "");
        if (text.startsWith("file:"))
            return text;
        const parts = text.replace(/\\/g, "/").split("/").map((p, i) => (i === 0 && /^[A-Za-z]:$/.test(p)) ? p : encodeURIComponent(p));
        const joined = parts.join("/");
        return "file://" + (joined.startsWith("/") ? "" : "/") + joined;
    }

    /// Pasta-mãe de um caminho local, como URL (ponto de partida dos diálogos).
    function parentUrl(path) {
        const text = String(path || "").replace(/\\/g, "/").replace(/\/+$/, "");
        const cut = text.lastIndexOf("/");
        return cut > 0 ? toUrl(text.slice(0, cut)) : homeFolder;
    }

    /// Lista de nomes para um aviso: até 20, e "… e mais N".
    function namesList(names) {
        const shown = names.slice(0, 20).map((n) => Messages.escapeHtml(n)).join("<br>");
        const rest = names.length - 20;
        return rest > 0 ? shown + "<br>" + qsTr("…and %n more", "", rest) : shown;
    }

    function openDialog() {
        openFolderDialog.currentFolder = App.hasProject ? parentUrl(App.projectPath) : homeFolder;
        openFolderDialog.open();
    }

    function newProject() {
        newFolderDialog.currentFolder = App.hasProject ? parentUrl(App.projectPath) : homeFolder;
        newFolderDialog.open();
    }

    function openRecent(project) {
        if (!project)
            return;
        // Pasta sumiu: a única coisa a fazer é tirá-la da lista.
        if (!project.exists) {
            removeRecent(project);
            return;
        }
        openFolder(toUrl(project.path));
    }

    function removeRecent(project) {
        Dialogs.confirm({
            title: qsTr("Remove from the list?"),
            description: qsTr("<b>%1</b> leaves the recent projects. The folder and its photos are not touched.")
                .arg(Messages.escapeHtml(project.name)),
            variant: "warning",
            confirmLabel: qsTr("Remove from list"),
            onConfirm: () => App.removeRecent(project.path)
        });
    }

    /// Abre a pasta escolhida; sem raw/, pergunta o que fazer (como o
    /// QMessageBox de antes): mover os JPEGs soltos / criar raw/, escolher
    /// outra pasta ou cancelar.
    function openFolder(folder) {
        const info = App.inspectFolder(folder);
        if (!info.exists) {
            Dialogs.alert(Messages.errorDialog("project.notFound", { path: info.path }));
            return;
        }
        if (info.hasRaw) {
            App.openProject(info.path);
            return;
        }
        const loose = info.looseJpegs;
        Dialogs.choose({
            title: qsTr("No raw/ folder"),
            description: Messages.escapeHtml(Messages.error("project.noRaw", { name: info.name })),
            variant: "warning",
            iconName: "folder-x",
            buttons: [
                { id: "create", primary: true, variant: "default",
                  label: loose > 0 ? qsTr("Move the %n photos into raw/ and continue", "", loose)
                                   : qsTr("Create raw/ and continue") },
                { id: "other", variant: "outline", label: qsTr("Choose another folder") },
                { id: "cancel", variant: "ghost", label: qsTr("Cancel") }
            ]
        }, (choice) => {
            if (choice === "other")
                openDialog();
            else if (choice === "create")
                _prepare(info, loose > 0, () => App.openProject(info.path));
        });
    }

    // Cria raw/ (e move os JPEGs soltos); avisa o que ficou para trás e segue.
    function _prepare(info, moveLoose, next) {
        const result = App.prepareFolder(info.path, moveLoose);
        if (!result.ok) {
            Dialogs.alert(Messages.errorDialog("project.rawFailed", { name: info.name }));
            return;
        }
        const failed = result.failed || [];
        if (failed.length > 0) {
            Dialogs.alert({
                title: App.brandName,
                description: qsTr("Some photos stayed where they were:") + "<br><br>" + namesList(failed),
                variant: "warning"
            }, next);
            return;
        }
        next();
    }

    // Novo projeto numa pasta escolhida (ou criada) no diálogo do SO:
    //  * já é um projeto -> só abre;
    //  * tem JPEGs soltos -> cria raw/, MOVE as fotos para ela e abre;
    //  * vazia (ou confirmada) -> cria raw/ e oferece copiar fotos de outro lugar.
    function _newIn(folder) {
        const info = App.inspectFolder(folder);
        if (!info.exists) {
            Dialogs.alert(Messages.errorDialog("project.notFound", { path: info.path }));
            return;
        }
        if (info.isProject) {
            App.openProject(info.path);
            return;
        }
        if (info.looseJpegs > 0) {
            _prepare(info, true, () => App.openProject(info.path));
            return;
        }
        const start = () => _prepare(info, false, () => {
            importDialog.projectPath = info.path;
            importDialog.currentFolder = homeFolder;
            importDialog.open();
        });
        if (!info.empty) {
            Dialogs.confirm({
                title: qsTr("Folder is not empty"),
                description: qsTr("The folder “%1” already has files. Use it as the new project anyway?")
                    .arg(Messages.escapeHtml(info.name)),
                variant: "warning",
                confirmLabel: qsTr("Use this folder")
            }, start);
            return;
        }
        start();
    }

    FolderDialog {
        id: openFolderDialog
        title: qsTr("Open project folder")
        onAccepted: flows.openFolder(selectedFolder)
    }

    FolderDialog {
        id: newFolderDialog
        title: qsTr("Choose or create the folder for the new project")
        onAccepted: flows._newIn(selectedFolder)
    }

    FileDialog {
        id: importDialog

        property string projectPath: ""

        title: qsTr("Choose the JPEG photos to add to the project")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("JPEG photos (*.jpg *.jpeg *.JPG *.JPEG)")]
        onAccepted: {
            flows._importing = projectPath;
            // Cópia assíncrona: App.loading mostra "Copiando fotos… x de y".
            App.importPhotos(projectPath, selectedFiles);
        }
        // Sem fotos agora: abre assim mesmo (dá para pôr em raw/ e reescanear).
        onRejected: App.openProject(projectPath)
    }

    Connections {
        target: App

        function onImportFinished(path, copied, failed) {
            if (path !== flows._importing)
                return;
            flows._importing = "";
            if (failed.length > 0) {
                Dialogs.alert({
                    title: App.brandName,
                    description: qsTr("Some photos could not be copied:") + "<br><br>" + flows.namesList(failed),
                    variant: "warning"
                }, () => App.openProject(path));
                return;
            }
            App.openProject(path);
        }
    }
}
