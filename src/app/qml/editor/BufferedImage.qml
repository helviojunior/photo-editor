import QtQuick

// Imagem que só troca depois que a nova terminou de carregar (o
// useLoadedImage.js do React): enquanto o slider é arrastado, a "Editada"
// anterior continua na tela em vez de piscar vazia a cada render. Duas
// Image alternam: a da frente mostra, a de trás carrega.
//
//   BufferedImage { anchors.fill: parent; source: App.editor.editedUrl }
//   opacity: buffered.loading ? 0.8 : 1
Item {
    id: root

    property url source
    property int fillMode: Image.Stretch
    /// Uma imagem nova está a caminho (a da frente ainda é a anterior).
    readonly property bool loading: _back.status === Image.Loading
    /// A da frente já está na tela.
    readonly property bool ready: _front.status === Image.Ready
    /// Altura/largura da imagem mostrada (0 = nada carregado ainda).
    readonly property real sourceAspect: _front.status === Image.Ready && _front.implicitWidth > 0
        ? _front.implicitHeight / _front.implicitWidth : 0

    property bool _aFront: true
    readonly property Image _front: _aFront ? a : b
    readonly property Image _back: _aFront ? b : a

    function _load() {
        const url = String(source);
        if (url === "") {
            a.source = "";
            b.source = "";
            return;
        }
        if (String(_front.source) === url && _front.status !== Image.Error) {
            _back.source = "";
            return;
        }
        _back.source = source;
    }

    function _settle(img) {
        if (img === _front || String(img.source) !== String(source) || String(source) === "")
            return;
        if (img.status === Image.Ready) {
            const old = _front;
            _aFront = img === a;
            old.source = "";
        }
    }

    onSourceChanged: _load()
    Component.onCompleted: _load()

    Image {
        id: a
        anchors.fill: parent
        fillMode: root.fillMode
        asynchronous: true
        cache: true
        smooth: true
        visible: root._aFront
        onStatusChanged: root._settle(a)
    }

    Image {
        id: b
        anchors.fill: parent
        fillMode: root.fillMode
        asynchronous: true
        cache: true
        smooth: true
        visible: !root._aFront
        onStatusChanged: root._settle(b)
    }
}
