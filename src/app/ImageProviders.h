// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Imagens do QML. Tudo que é gerado (preview, thumbnail, render, máscara,
// recorte do merge, capa da Home) vem de um provider ASSÍNCRONO: a geração roda
// no pool de threads e a interface nunca trava.
//
// O QML não fala com o banco: o controller (thread principal) monta o pedido
// (o que renderizar, de onde) e o registra no ImageRegistry com uma chave
// determinística; a URL leva só a chave — image://photo/render/<chave>.
//
//   image://photo/thumb/<chave>        thumbnail (240 px) da filmstrip
//   image://photo/preview/<chave>      preview do original (1600 px)
//   image://photo/render/<chave>       "Editada" (crop + motor + camadas)
//   image://photo/mask/<máscara>       máscara como overlay (branco + alpha)
//   image://photo/cutout/<chave>       área de uma camada do merge (PNG RGBA)
//   image://photo/file/<chave>         um arquivo pronto (capa da Home etc.)
//   image://icon/<nome>?color=#rrggbb  ícone Lucide recolorido

#include "core/Derivatives.h"

#include <QHash>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

#include <functional>

/// Pedidos de imagem registrados pelos controllers, por chave.
class ImageRegistry
{
public:
    using Producer = std::function<QImage()>;

    static ImageRegistry &instance();

    /// Registra quem produz a imagem de ``key`` (qualquer thread).
    void put(const QString &kind, const QString &key, Producer producer);
    Producer take(const QString &kind, const QString &key) const;
    /// Esquece tudo (projeto fechado).
    void clear();

private:
    mutable QMutex m_lock;
    QHash<QString, Producer> m_producers;
};

class PhotoImageProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

namespace imagefiles {

/// Lê uma imagem (JPEG/PNG) de disco em QImage, reduzindo para ``requested``.
QImage load(const QString &path, const QSize &requested = {});
/// cv::Mat RGB/RGBA -> QImage (cópia).
QImage fromMat(const cv::Mat &mat);

} // namespace imagefiles
