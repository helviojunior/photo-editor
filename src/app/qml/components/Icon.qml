import QtQuick
import PhotoEditor

// Ícone Lucide (src/app/icons/<name>.svg) recolorido pelo provider
// image://icon. Renderiza na densidade da tela, então fica nítido em HiDPI.
//
//   Icon { name: "folder-open"; color: Theme.brand400; size: 20 }
//
// O SVG não aceita cor com alfa: a transparência de ``color`` vira a
// ``opacity`` do item.
Image {
    id: icon

    /// Nome do arquivo em src/app/icons, sem ".svg" ("" = nada).
    property string name: ""
    property color color: Theme.foreground
    /// Lado do quadrado, em px lógicos.
    property int size: 16

    width: size
    height: size
    sourceSize: Qt.size(Math.ceil(size * Screen.devicePixelRatio), Math.ceil(size * Screen.devicePixelRatio))
    source: name ? Theme.iconUrl(name, color) : ""
    opacity: color.a
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: false
    cache: true
}
