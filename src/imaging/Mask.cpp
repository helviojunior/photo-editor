// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Mask.h"

#include "imaging/Develop.h"
#include "imaging/SettingsHash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <list>
#include <mutex>
#include <utility>
#include <vector>

namespace pe::imaging {

namespace {

Q_LOGGING_CATEGORY(lcMask, "photoeditor.imaging.mask")

constexpr size_t kLoadCache = 16;

/// Cache das máscaras decodificadas (o lru_cache(16) do Python), pelo caminho.
/// Um "não existe" não fica em cache: o arquivo pode aparecer depois.
std::mutex g_cacheMutex;
std::list<std::pair<QString, cv::Mat>> g_cache;  ///< recente no fim

cv::Mat cachedMask(const QString &path)
{
    {
        std::lock_guard lock(g_cacheMutex);
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
            if (it->first == path) {
                g_cache.splice(g_cache.end(), g_cache, it);
                return g_cache.back().second;
            }
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    const cv::Mat buf(1, static_cast<int>(data.size()), CV_8UC1, const_cast<char *>(data.constData()));
    cv::Mat mask = cv::imdecode(buf, cv::IMREAD_GRAYSCALE);
    if (mask.empty())
        return {};
    std::lock_guard lock(g_cacheMutex);
    g_cache.emplace_back(path, mask);
    while (g_cache.size() > kLoadCache)
        g_cache.pop_front();
    return mask;
}

bool writeAtomic(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    return file.commit();
}

QByteArray encodePng(const cv::Mat &img, int compression)
{
    std::vector<uchar> buf;
    if (!cv::imencode(".png", img, buf, {cv::IMWRITE_PNG_COMPRESSION, compression}))
        return {};
    return QByteArray(reinterpret_cast<const char *>(buf.data()), static_cast<qsizetype>(buf.size()));
}

} // namespace

std::optional<StrokeMode> strokeModeByName(const QString &name)
{
    if (name == QLatin1String("add"))
        return StrokeMode::Add;
    if (name == QLatin1String("subtract"))
        return StrokeMode::Subtract;
    return std::nullopt;
}

QString maskPath(const QString &masksDir, const QString &key)
{
    return QDir(masksDir).filePath(key + QStringLiteral(".png"));
}

QString saveMask(const QString &masksDir, const cv::Mat &alpha)
{
    // (clip(alpha, 0, 1) * 255 + 0,5) truncado, como o astype(uint8).
    // O convertTo para 8 bits arredonda; a conta vai à mão, em float32 como
    // no numpy.
    cv::Mat f;
    alpha.convertTo(f, CV_32F);
    cv::Mat u8(f.size(), CV_8UC1);
    for (int y = 0; y < f.rows; ++y) {
        const float *s = f.ptr<float>(y);
        uchar *d = u8.ptr<uchar>(y);
        for (int x = 0; x < f.cols; ++x) {
            const float v = std::clamp(s[x], 0.0f, 1.0f) * 255.0f;
            d[x] = static_cast<uchar>(v + 0.5f);
        }
    }
    // Pillow com optimize=True usa compressão 9; os bytes ainda diferem (ver
    // o cabeçalho), então a chave de uma máscara nova não é a do Python.
    const QByteArray data = encodePng(u8, 9);
    if (data.isEmpty()) {
        qCWarning(lcMask) << "Could not encode the layer mask";
        return {};
    }
    const QString key = sha1Hex(data).left(kMaskKeyLength);
    const QString path = maskPath(masksDir, key);
    if (!QFileInfo(path).isFile()) {
        if (!QDir().mkpath(masksDir) || !writeAtomic(path, data)) {
            qCWarning(lcMask) << "Could not write the layer mask" << path;
            return {};
        }
    }
    return key;
}

cv::Mat loadMask(const QString &masksDir, const QString &key)
{
    if (!isMaskKey(key))
        return {};
    const QString path = maskPath(masksDir, key);
    if (!QFileInfo(path).isFile())
        return {};
    return cachedMask(path).clone();
}

QByteArray overlayPng(const QString &masksDir, const QString &key)
{
    const cv::Mat mask = loadMask(masksDir, key);
    if (mask.empty())
        return {};
    const cv::Mat white(mask.size(), CV_8UC1, cv::Scalar(255));
    cv::Mat rgba;
    cv::merge(std::vector<cv::Mat>{white, white, white, mask}, rgba);
    // Pillow sem optimize: compress_level 6, o padrão do zlib.
    return encodePng(rgba, 6);
}

cv::Mat combineStroke(const cv::Mat &current, const cv::Mat &region, StrokeMode mode)
{
    cv::Mat cur;
    if (!current.empty() && current.size() == region.size())
        current.convertTo(cur, CV_32F, 1.0 / 255.0);
    else
        cur = cv::Mat::zeros(region.size(), CV_32FC1);
    cv::Mat out;
    if (mode == StrokeMode::Subtract) {
        const cv::Mat inv = 1.0 - region;
        out = cv::min(cur, inv);
    } else {
        out = cv::max(cur, region);
    }
    cv::Mat over;
    cv::compare(out, 0.5, over, cv::CMP_GT);
    if (cv::countNonZero(over) == 0)
        return {};
    return out;
}

QString applyStroke(const QString &masksDir, const QString &baseKey, const cv::Mat &region, StrokeMode mode)
{
    const cv::Mat current = baseKey.isEmpty() ? cv::Mat() : loadMask(masksDir, baseKey);
    const cv::Mat out = combineStroke(current, region, mode);
    if (out.empty())
        return {};
    return saveMask(masksDir, out);
}

double coverage(const QString &masksDir, const QString &key)
{
    const cv::Mat mask = loadMask(masksDir, key);
    if (mask.empty())
        return 0.0;
    cv::Mat over;
    cv::compare(mask, 127, over, cv::CMP_GT);
    return static_cast<double>(cv::countNonZero(over)) / static_cast<double>(mask.total());
}

cv::Mat layerAlpha(const QString &masksDir, const QString &key, cv::Size fullSize, const Crop &crop,
                   cv::Size outSize)
{
    cv::Mat mask = loadMask(masksDir, key);
    if (mask.empty()) {
        qCWarning(lcMask) << "Layer mask" << key << "is missing; the layer is skipped.";
        return {};
    }
    if (mask.size() != fullSize) {
        cv::Mat r;
        cv::resize(mask, r, fullSize, 0, 0, cv::INTER_LINEAR);
        mask = r;
    }
    mask = applyCrop(mask, crop);
    if (mask.size() != outSize) {
        cv::Mat r;
        cv::resize(mask, r, outSize, 0, 0, cv::INTER_AREA);
        mask = r;
    }
    cv::Mat out;
    mask.convertTo(out, CV_32F, 1.0 / 255.0);
    return out;
}

} // namespace pe::imaging
