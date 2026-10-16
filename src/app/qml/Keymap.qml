pragma Singleton
import QtQuick
import QtCore
import PhotoEditor

// Registro dos atalhos de teclado (o Keymap do clivcut): o padrão de cada ação
// e as trocas da pessoa, salvas nas preferências (QSettings, "shortcuts").
// Quem dispara continua sendo cada tela (Shortcut/Action com as condições
// dela); daqui sai só a SEQUÊNCIA, então trocar um atalho vale na hora.
//
//   Shortcut { sequences: Keymap.keys("nextPhoto"); onActivated: ... }
//   Text { text: Keymap.label("palette") }          // "Ctrl+K" / "⌘K"
//   Keymap.assign("nextPhoto", "N")                 // tira o N de quem o usava
//
// Sequências no formato portável do QKeySequence ("Ctrl+Shift+Z"; no macOS o
// Ctrl é o ⌘). Para mostrar: App.shortcutText(seq); para comparar:
// App.sameShortcut(a, b).
QtObject {
    id: keymap

    /// {idDaAção: ["Ctrl+B", ...]} — só o que difere do padrão.
    property string custom: "{}"

    property Settings store: Settings {
        category: "shortcuts"
        property alias custom: keymap.custom
    }

    readonly property var overrides: {
        try {
            const o = JSON.parse(keymap.custom);
            return o && typeof o === "object" ? o : {};
        } catch (e) {
            return {};
        }
    }

    /// Gravando um atalho no editor de atalhos: as telas desligam os seus.
    property bool recording: false

    readonly property bool _mac: Qt.platform.os === "osx"

    // Ordem = ordem dos grupos e das linhas no editor de atalhos.
    readonly property var actions: [
        { id: "nextPhoto", group: qsTr("Photos"), name: qsTr("Next photo (rotates the frame in crop)"), icon: "chevron-right", keys: ["Right"] },
        { id: "previousPhoto", group: qsTr("Photos"), name: qsTr("Previous photo (rotates the frame in crop)"), icon: "chevron-left", keys: ["Left"] },
        // No Mac o ⌫ é o Backspace (arquivar) e o fn+⌫ é o Del (lixeira).
        { id: "archivePhoto", group: qsTr("Photos"), name: qsTr("Archive photo"), icon: "archive", keys: ["Backspace"] },
        { id: "deletePhoto", group: qsTr("Photos"), name: qsTr("Move to trash"), icon: "trash-2", keys: ["Del"] },
        { id: "restorePhoto", group: qsTr("Photos"), name: qsTr("Restore photo"), icon: "archive-restore", keys: ["Ctrl+Shift+R"] },
        { id: "duplicate", group: qsTr("Photos"), name: qsTr("Duplicate photo"), icon: "copy", keys: ["Ctrl+D"] },
        { id: "cover", group: qsTr("Photos"), name: qsTr("Set or unset cover"), icon: "star", keys: [] },
        { id: "instagram", group: qsTr("Photos"), name: qsTr("Instagram version"), icon: "instagram", keys: ["I"] },
        { id: "rescan", group: qsTr("Photos"), name: qsTr("Rescan raw/"), icon: "refresh-cw", keys: [] },
        { id: "merge", group: qsTr("Photos"), name: qsTr("Select photos to merge"), icon: "combine", keys: [] },
        { id: "showPhotos", group: qsTr("Photos"), name: qsTr("Show photos"), icon: "image", keys: [] },
        { id: "showArchived", group: qsTr("Photos"), name: qsTr("Show archived photos"), icon: "archive", keys: [] },
        { id: "showTrash", group: qsTr("Photos"), name: qsTr("Show trash"), icon: "trash-2", keys: [] },
        { id: "purgePhoto", group: qsTr("Photos"), name: qsTr("Delete permanently"), icon: "trash-2", keys: [] },
        { id: "emptyTrash", group: qsTr("Photos"), name: qsTr("Empty trash"), icon: "trash-2", keys: [] },

        { id: "undo", group: qsTr("Editing"), name: qsTr("Undo"), icon: "undo-2", keys: ["Ctrl+Z"] },
        { id: "redo", group: qsTr("Editing"), name: qsTr("Redo"), icon: "redo-2", keys: ["Ctrl+Shift+Z", "Ctrl+Y"] },
        { id: "auto", group: qsTr("Editing"), name: qsTr("Auto"), icon: "wand-sparkles", keys: ["A"] },
        { id: "reset", group: qsTr("Editing"), name: qsTr("Reset adjustments"), icon: "rotate-ccw", keys: [] },
        { id: "crop", group: qsTr("Editing"), name: qsTr("Crop"), icon: "crop", keys: ["C"] },
        { id: "nextLayer", group: qsTr("Editing"), name: qsTr("Next layer"), icon: "layers", keys: ["L"] },

        { id: "selectArea", group: qsTr("Selection"), name: qsTr("Select area (applies when selecting)"), icon: "brush", keys: ["S"] },
        { id: "applySelection", group: qsTr("Selection"), name: qsTr("Apply selection"), icon: "check", keys: ["Return", "Enter"] },
        { id: "cancel", group: qsTr("Selection"), name: qsTr("Cancel selection or crop"), icon: "x", keys: ["Esc"] },
        { id: "brushSmaller", group: qsTr("Selection"), name: qsTr("Smaller brush"), icon: "brush", keys: ["["] },
        { id: "brushLarger", group: qsTr("Selection"), name: qsTr("Larger brush"), icon: "brush", keys: ["]"] },

        { id: "export", group: qsTr("Output"), name: qsTr("Export"), icon: "upload", keys: ["Ctrl+E"] },
        { id: "publish", group: qsTr("Output"), name: qsTr("Publish on Instagram"), icon: "instagram", keys: [] },

        { id: "newProject", group: qsTr("File"), name: qsTr("New project"), icon: "folder-plus", keys: ["Ctrl+N"] },
        { id: "openProject", group: qsTr("File"), name: qsTr("Open project"), icon: "folder-open", keys: ["Ctrl+O"] },
        { id: "closeProject", group: qsTr("File"), name: qsTr("Close project"), icon: "folder-x", keys: ["Ctrl+W"] },
        { id: "home", group: qsTr("File"), name: qsTr("Projects"), icon: "folder", keys: ["Ctrl+Shift+H"] },
        { id: "quit", group: qsTr("File"), name: qsTr("Quit"), icon: "log-out", keys: ["Ctrl+Q"] },

        { id: "palette", group: qsTr("Interface"), name: qsTr("Command palette"), icon: "search", keys: ["Ctrl+K"] },
        { id: "shortcuts", group: qsTr("Interface"), name: qsTr("Keyboard shortcuts"), icon: "keyboard", keys: ["Ctrl+/"] },
        { id: "fullScreen", group: qsTr("Interface"), name: qsTr("Full screen"), icon: "maximize", keys: [_mac ? "Ctrl+Meta+F" : "F11"] },
        { id: "theme", group: qsTr("Interface"), name: qsTr("Light or dark theme"), icon: "sun", keys: [] }
    ]

    function action(id) {
        return actions.find((a) => a.id === id) || null;
    }

    function defaults(id) {
        const a = action(id);
        return a ? a.keys : [];
    }

    /// Atalhos da ação: os da pessoa ou, sem troca, os padrões.
    function keys(id) {
        const o = overrides[id];
        return o !== undefined ? o : defaults(id);
    }

    /// Só o primeiro atalho ("" se nenhum) — o da Action dos menus.
    function first(id) {
        const k = keys(id);
        return k.length > 0 ? k[0] : "";
    }

    function isCustom(id) {
        return overrides[id] !== undefined;
    }

    /// Primeiro atalho no formato da plataforma ("Ctrl+K", "⌘K"), ou "".
    function label(id) {
        const k = first(id);
        return k !== "" ? App.shortcutText(k) : "";
    }

    /// " (Ctrl+K)" para pendurar num rótulo/dica, ou "" sem atalho.
    function hint(id) {
        const l = label(id);
        return l !== "" ? " (" + l + ")" : "";
    }

    function setKeys(id, list) {
        const o = Object.assign({}, overrides);
        // Igual ao padrão: volta a seguir o padrão (mudanças futuras dele valem).
        if (JSON.stringify(list) === JSON.stringify(defaults(id)))
            delete o[id];
        else
            o[id] = list;
        keymap.custom = JSON.stringify(o);
    }

    function reset(id) {
        const o = Object.assign({}, overrides);
        delete o[id];
        keymap.custom = JSON.stringify(o);
    }

    function resetAll() {
        keymap.custom = "{}";
    }

    /// Ação que já usa ``seq`` (exceto ``exceptId``), ou null.
    function owner(seq, exceptId) {
        for (const a of actions) {
            if (a.id !== exceptId && keys(a.id).some((k) => App.sameShortcut(k, seq)))
                return a;
        }
        return null;
    }

    /// Dá ``seq`` a ``id``, tirando-o de quem já o usava. Devolve o nome da
    /// ação que perdeu o atalho, ou "".
    function assign(id, seq) {
        const other = owner(seq, id);
        if (other)
            setKeys(other.id, keys(other.id).filter((k) => !App.sameShortcut(k, seq)));
        const mine = keys(id);
        if (!mine.some((k) => App.sameShortcut(k, seq)))
            setKeys(id, mine.concat([seq]));
        return other ? other.name : "";
    }

    function remove(id, seq) {
        setKeys(id, keys(id).filter((k) => k !== seq));
    }

    /// Sequência portável de uma tecla capturada (Keys.onPressed), ou "" se
    /// a tecla não serve de atalho. No macOS o Qt já entrega o ⌘ como Control.
    function sequenceFor(key, modifiers) {
        const names = {};
        names[Qt.Key_Left] = "Left"; names[Qt.Key_Right] = "Right";
        names[Qt.Key_Up] = "Up"; names[Qt.Key_Down] = "Down";
        names[Qt.Key_Delete] = "Del"; names[Qt.Key_Backspace] = "Backspace";
        names[Qt.Key_Return] = "Return"; names[Qt.Key_Enter] = "Enter";
        names[Qt.Key_Escape] = "Esc"; names[Qt.Key_Tab] = "Tab"; names[Qt.Key_Backtab] = "Tab";
        names[Qt.Key_Space] = "Space"; names[Qt.Key_Home] = "Home"; names[Qt.Key_End] = "End";
        names[Qt.Key_PageUp] = "PgUp"; names[Qt.Key_PageDown] = "PgDown"; names[Qt.Key_Insert] = "Ins";
        let name = names[key] || "";
        if (name === "" && key >= Qt.Key_F1 && key <= Qt.Key_F35)
            name = "F" + (key - Qt.Key_F1 + 1);
        // Letras, números e pontuação: o código da tecla é o próprio ASCII.
        if (name === "" && key >= 0x21 && key <= 0x7e)
            name = String.fromCharCode(key).toUpperCase();
        if (name === "")
            return "";
        let prefix = "";
        if (modifiers & Qt.ControlModifier) prefix += "Ctrl+";
        if (modifiers & Qt.AltModifier) prefix += "Alt+";
        if (modifiers & Qt.ShiftModifier) prefix += "Shift+";
        if (modifiers & Qt.MetaModifier) prefix += "Meta+";
        return prefix + name;
    }
}
