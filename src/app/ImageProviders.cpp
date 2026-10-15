// SPDX-License-Identifier: BSD-2-Clause
#include "ImageProviders.h"

#include "imaging/Mask.h"

#include <QColor>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QRunnable>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QUrlQuery>

#include <opencv2/imgproc.hpp>

namespace {

class Response : public QQuickImageResponse, public QRunnable
{
public:
    Response(ImageRegistry::Producer producer, const QSize &requested)
        : m_producer(std::move(producer))
        , m_requested(requested)
    {
        setAutoDelete(false);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("image unavailable") : QString();
    }

    void run() override
    {
        try {
            if (m_producer)
                m_image = m_producer();
        } catch (const std::exception &e) {
            qWarning("image provider: %s", e.what());
        }
        if (!m_image.isNull() && m_requested.isValid() && m_requested.width() > 0 && m_requested.height() > 0
            && (m_image.width() > m_requested.width() || m_image.height() > m_requested.height()))
            m_image = m_image.scaled(m_requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        emit finished();
    }

private:
    ImageRegistry::Producer m_producer;
    QSize m_requested;
    QImage m_image;
};

QThreadPool &imagePool()
{
    // Pool próprio: o render da "Editada" não espera a fila de thumbnails.
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(std::max(2, QThread::idealThreadCount() - 1));
        return p;
    }();
    return *pool;
}

} // namespace

ImageRegistry &ImageRegistry::instance()
{
    static ImageRegistry registry;
    return registry;
}

void ImageRegistry::put(const QString &kind, const QString &key, Producer producer)
{
    QMutexLocker locker(&m_lock);
    m_producers.insert(kind + u'/' + key, std::move(producer));
}

ImageRegistry::Producer ImageRegistry::take(const QString &kind, const QString &key) const
{
    QMutexLocker locker(&m_lock);
    return m_producers.value(kind + u'/' + key);
}

void ImageRegistry::clear()
{
    QMutexLocker locker(&m_lock);
    m_producers.clear();
}

QQuickImageResponse *PhotoImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // id = "<tipo>/<chave>[?...]"; a query só serve para mudar a URL — menos
    // ``color`` nas máscaras: o overlay (branco + alfa) sai pintado nessa cor,
    // que é do tema (o acento da marca), não do arquivo.
    const QString path = id.section(u'?', 0, 0);
    const QString kind = path.section(u'/', 0, 0);
    const QString key = path.section(u'/', 1);
    ImageRegistry::Producer producer = ImageRegistry::instance().take(kind, key);
    const QColor tint(QUrlQuery(id.section(u'?', 1)).queryItemValue(QStringLiteral("color"), QUrl::FullyDecoded));
    if (producer && kind == QLatin1String("mask") && tint.isValid()) {
        producer = [producer, tint]() {
            QImage image = producer().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (!image.isNull()) {
                QPainter painter(&image);
                painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                painter.fillRect(image.rect(), tint);
            }
            return image;
        };
    }
    auto *response = new Response(std::move(producer), requestedSize);
    imagePool().start(response);
    return response;
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // id = "<nome>?color=#rrggbb[&fill=1]": troca o currentColor do SVG do
    // Lucide; ``fill`` preenche o desenho na mesma cor (a estrela da capa).
    const QString name = id.section(u'?', 0, 0);
    const QUrlQuery query(id.section(u'?', 1));
    const QString color = query.queryItemValue(QStringLiteral("color"));
    QFile f(QStringLiteral(":/qt/qml/PhotoEditor/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray svg = f.readAll();
    if (query.queryItemValue(QStringLiteral("fill")) == QLatin1String("1"))
        svg.replace("fill=\"none\"", "fill=\"currentColor\"");
    if (!color.isEmpty())
        svg.replace("currentColor", color.toLatin1());
    QSvgRenderer renderer(svg);
    const QSize out = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(24, 24);
    QImage image(out, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    if (size)
        *size = out;
    return image;
}

namespace imagefiles {

QImage load(const QString &path, const QSize &requested)
{
    QImageReader reader(path);
    // A orientação já vem aplicada nos derivados; nos originais o EXIF manda.
    reader.setAutoTransform(true);
    if (requested.isValid() && !requested.isEmpty()) {
        QSize s = reader.size();
        if (s.isValid() && (s.width() > requested.width() || s.height() > requested.height()))
            reader.setScaledSize(s.scaled(requested, Qt::KeepAspectRatio));
    }
    return reader.read();
}

QImage fromMat(const cv::Mat &mat)
{
    if (mat.empty())
        return {};
    if (mat.type() == CV_8UC3)
        return QImage(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step), QImage::Format_RGB888).copy();
    if (mat.type() == CV_8UC4)
        return QImage(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step), QImage::Format_RGBA8888).copy();
    if (mat.type() == CV_8UC1)
        return QImage(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step), QImage::Format_Grayscale8).copy();
    return {};
}

} // namespace imagefiles
