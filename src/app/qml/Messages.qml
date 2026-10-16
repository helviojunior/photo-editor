pragma Singleton
import QtQuick

// Textos das chaves que o C++ manda ao QML: erros (sinal ``failed(key,
// params)`` do App/editor/merge/instagram) e mensagens de carregamento
// (App.loading). Os textos em inglês são a fonte do qsTr (o padrão e o
// fallback); a tradução vem dos .ts.
//
//   Dialogs.alert(Messages.errorDialog(key, params))
//   Text { text: Messages.loading(App.loading, App.loadingParams) }
QtObject {
    id: messages

    /// Escapa texto para StyledText (nomes de arquivo com < > &).
    function escapeHtml(text) {
        return String(text === undefined || text === null ? "" : text)
            .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    /// Último componente de um caminho ("/a/b/Evento" -> "Evento").
    function baseName(path) {
        const parts = String(path || "").split(/[\\/]+/).filter((p) => p !== "");
        return parts.length ? parts[parts.length - 1] : String(path || "");
    }

    // ------------------------------------------------------------------ //
    // Erros
    // ------------------------------------------------------------------ //

    /// Mensagem de uma chave de erro. Chave desconhecida = a genérica.
    function error(key, params) {
        const p = params || {};
        const name = p.name !== undefined ? String(p.name) : "";
        const path = p.path !== undefined ? String(p.path) : "";
        switch (key) {
        // Projeto
        case "project.notFound":
            return qsTr("The folder “%1” no longer exists or cannot be read.").arg(path || name);
        case "project.noRaw":
            return qsTr("The folder “%1” has no raw/ subfolder, which is where the original JPEGs of the event go.")
                .arg(name || baseName(path));
        case "project.locked":
            return qsTr("The project “%1” is already open in another %2 window.").arg(name).arg(App.brandName);
        case "project.openFailed":
            return qsTr("The project “%1” could not be opened.").arg(name || baseName(path));
        case "project.rawFailed":
            return qsTr("Could not create the raw/ folder in “%1”.").arg(name || baseName(path));

        // Genéricos
        case "error.unexpected":
        case "error.generic":
        case "protocol.badRequest":
            return qsTr("Something went wrong. Please try again.");
        case "error.notFound":
            return qsTr("Not found.");
        case "error.photoNotFound":
            return qsTr("The photo is no longer in the catalog.");
        case "error.photoFileMissing":
            return qsTr("The file %1 is no longer where the catalog expected it.").arg(name);
        case "error.restoreConflict":
            return qsTr("Cannot restore %1: there is already a file with that name in raw/.").arg(name);

        // Conta-gotas do balanço de branco
        case "wb.badSample":
            return qsTr("The chosen area is too dark or too blown out for white balance. Pick a light, neutral area.");

        // Camadas e merge
        case "layers.maskNotFound":
            return qsTr("The selection no longer exists. Start a new one.");
        case "layers.invalidStroke":
            return qsTr("The brush stroke is empty or invalid.");
        case "merge.tooFew":
            return qsTr("Select at least two photos: the first is the base, the others become layers.");
        case "merge.alreadyMerged":
            return qsTr("Already part of a merge: %1. Undo that merge first.").arg(name);
        case "merge.notALayer":
            return qsTr("This photo is not a layer of the merge.");
        case "merge.instagramVersion":
            return qsTr("An Instagram version cannot be part of a merge: it already starts from the merge of its photo.");
        case "merge.notFound":
            return qsTr("This merge no longer exists.");

        // Instagram
        case "instagram.unavailable":
            return qsTr("The Instagram helper is not installed with this copy of the app, so publishing is unavailable.");
        case "instagram.notForVersion":
            return qsTr("Not available for the Instagram version: use the original photo.");
        case "instagram.versionExists":
            return qsTr("This photo already has another Instagram version.");
        case "instagram.notConnected":
            return qsTr("No Instagram account is connected. Connect one in Settings > Instagram.");
        case "instagram.missingCredentials":
            return qsTr("Enter the Instagram username and password.");
        case "instagram.missingCode":
            return qsTr("Enter the code.");
        case "instagram.noCodePending":
            return qsTr("There is no login waiting for a code. Start again.");
        case "instagram.badCredentials":
            return qsTr("Wrong username or password.");
        case "instagram.badCode":
            return qsTr("The code was not accepted. Try again with a new code.");
        case "instagram.challenge":
            return qsTr("Instagram asked to confirm this login. Open the Instagram app, confirm it was you and try again.");
        case "instagram.sessionExpired":
            return qsTr("The Instagram session has expired. Connect the account again in Settings > Instagram.");
        case "instagram.wait":
            return qsTr("Instagram is limiting requests from this account. Wait a few minutes and try again.");
        case "instagram.suspended":
            return qsTr("Instagram reports that this account is suspended.");
        case "instagram.network":
            return qsTr("Could not reach Instagram. Check the internet connection.");
        case "instagram.failed":
            return qsTr("Instagram did not accept the request.");
        case "instagram.publishFailed":
            return qsTr("The post could not be published.");
        case "instagram.publishRunning":
            return qsTr("A post is already being published.");
        case "instagram.noPhotos":
            return qsTr("Choose at least one Instagram version to publish.");
        case "instagram.outOfFeed":
            return qsTr("Outside the formats the feed accepts (a 1.91:1 frame turned past 45° becomes vertical 1:1.91): %1. Turn the frame back or choose another format.").arg(name);
        case "instagram.tooMany":
            return qsTr("An Instagram post takes at most %1 photos.").arg(p.max !== undefined ? p.max : 10);
        case "instagram.captionTooLong":
            return qsTr("The caption is longer than the %1 characters Instagram accepts.").arg(p.max !== undefined ? p.max : 2200);
        }
        if (key)
            console.warn("No message for error key", key);
        return qsTr("Something went wrong. Please try again.");
    }

    /// Título do modal de erro: o da situação, quando há um; senão o genérico.
    function errorTitle(key) {
        switch (key) {
        case "project.notFound": return qsTr("Folder not found");
        case "project.noRaw": return qsTr("No raw/ folder");
        case "project.locked": return qsTr("Project already open");
        case "project.openFailed": return qsTr("Could not open the project");
        case "project.rawFailed": return qsTr("Could not create raw/");
        case "instagram.unavailable": return qsTr("Instagram unavailable");
        case "wb.badSample": return qsTr("White balance");
        }
        if (String(key).startsWith("instagram."))
            return qsTr("Instagram");
        if (String(key).startsWith("merge."))
            return qsTr("Merge");
        return qsTr("Something went wrong");
    }

    /// Opções prontas para Dialogs.alert(): título, mensagem e o detalhe
    /// técnico (params.detail), quando vier.
    function errorDialog(key, params) {
        const p = params || {};
        let description = escapeHtml(error(key, p));
        if (p.detail)
            description += "<br><br><small>" + escapeHtml(p.detail) + "</small>";
        return { title: errorTitle(key), description: description, variant: "danger", iconName: "circle-alert" };
    }

    // ------------------------------------------------------------------ //
    // Editor: ações do histórico e a barra da filmstrip
    // ------------------------------------------------------------------ //

    /// Nome de uma ação do histórico (HistoryEntry.kind).
    function actionName(kind) {
        switch (kind) {
        case "delete": return qsTr("Delete");
        case "archive": return qsTr("Archive");
        case "restore": return qsTr("Restore");
        case "adjust": return qsTr("Adjustment");
        case "auto": return qsTr("Auto");
        case "preset": return qsTr("Preset");
        case "reset": return qsTr("Reset");
        case "crop": return qsTr("Crop");
        case "layer": return qsTr("Layers");
        case "duplicate": return qsTr("Duplicate");
        case "cover": return qsTr("Cover");
        case "instagram": return qsTr("Instagram version");
        case "levels": return qsTr("Levels");
        case "curves": return qsTr("Curves");
        case "effects": return qsTr("Effects");
        }
        return String(kind || "");
    }

    /// Nome de um efeito da biblioteca (App.editor.effectCatalog[].id): a aba
    /// Efeitos e a paleta de comandos.
    function effectName(id) {
        switch (id) {
        case "bw": return qsTr("B&W");
        case "sepia": return qsTr("Sepia");
        case "vintage": return qsTr("Vintage");
        case "fade": return qsTr("Fade");
        case "matte": return qsTr("Matte");
        case "vignette": return qsTr("Vignette");
        case "grain": return qsTr("Grain");
        }
        return String(id || "");
    }

    /// Nome de um preset de revelação (App.editor.presets[].id): o painel de
    /// edição e a paleta de comandos.
    function presetName(id) {
        switch (id) {
        case "vivid": return qsTr("Vivid");
        case "soft": return qsTr("Soft");
        case "warm": return qsTr("Warm");
        case "cool": return qsTr("Cool");
        case "bw": return qsTr("B&W");
        case "matte": return qsTr("Matte");
        case "vintage": return qsTr("Vintage");
        }
        return String(id || "");
    }

    /// Mensagem da barra da filmstrip (App.editor.statusMessage/statusParams).
    function status(key, params) {
        const p = params || {};
        const name = p.name !== undefined ? String(p.name) : "";
        switch (key) {
        case "status.deleted":
            return qsTr("%1 moved to deleted/.").arg(name);
        case "status.archived":
            return qsTr("Archived: %1").arg(name);
        case "status.restored":
            return qsTr("Restored: %1").arg(name);
        case "status.purged":
            return qsTr("Permanently deleted: %1").arg(name);
        case "status.trashEmptied":
            return qsTr("%n photo(s) permanently deleted.", "", p.count !== undefined ? Number(p.count) : 0);
        case "status.duplicated":
            return qsTr("%1 created: same file, its own adjustments.").arg(name);
        case "status.instagramCreated":
            return qsTr("Instagram version created: %1.").arg(name);
        case "status.undone":
            return qsTr("Undone: %1.").arg(actionName(p.kind));
        case "status.nothingToUndo":
            return qsTr("Nothing to undo.");
        case "status.redone":
            return name !== ""
                ? qsTr("Redone: %1 · %2").arg(actionName(p.kind)).arg(name)
                : qsTr("Redone: %1.").arg(actionName(p.kind));
        case "status.nothingToRedo":
            return qsTr("Nothing to redo.");
        case "status.rescanned":
            return qsTr("Catalog updated: %1 photos.").arg(p.total !== undefined ? p.total : 0);
        case "status.coverSet":
            return qsTr("%1 is the cover: it will be exported as capa.jpg.").arg(name);
        case "status.coverCleared":
            return qsTr("The project has no cover now.");
        }
        return "";
    }

    // ------------------------------------------------------------------ //
    // Carregamento (App.loading)
    // ------------------------------------------------------------------ //

    function loading(key, params) {
        const p = params || {};
        switch (key) {
        case "loading.opening":
            return qsTr("Opening “%1”…").arg(p.name || "");
        case "loading.importing":
            return qsTr("Copying photos… %1 of %2").arg(p.done || 0).arg(p.total || 0);
        case "loading.home":
            return qsTr("Starting…");
        }
        return key ? qsTr("Loading…") : "";
    }

    function loadingHint(key) {
        if (key === "loading.opening")
            return qsTr("Reading the catalog. Large projects take a few seconds.");
        return "";
    }
}
