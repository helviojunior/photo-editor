// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Segment.h"

#include "imaging/Develop.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>

#ifdef PHOTOEDITOR_HAVE_ONNX
#include <onnxruntime_cxx_api.h>

#include <array>
#include <list>
#include <string>
#include <utility>
#endif

namespace pe::imaging {

namespace {

/// round() do Python: meio para o par (modo padrão do nearbyint).
int pyRound(double v)
{
    return static_cast<int>(std::nearbyint(v));
}

/// Máscara "bool" do numpy: tudo que não é zero vira 255.
cv::Mat binary(const cv::Mat &mask)
{
    cv::Mat out;
    cv::compare(mask, 0, out, cv::CMP_NE);
    return out;
}

} // namespace

// --------------------------------------------------------------------------- //
// Traço -> área pintada
// --------------------------------------------------------------------------- //

cv::Mat paint(cv::Size size, const std::vector<cv::Point2d> &points, double radius)
{
    const int h = size.height, w = size.width;
    const int r = std::max(pyRound(radius * std::max(h, w)), 1);
    std::vector<cv::Point> pts;
    pts.reserve(points.size());
    for (const cv::Point2d &p : points)
        pts.emplace_back(pyRound(p.x * w), pyRound(p.y * h));
    cv::Mat out = cv::Mat::zeros(h, w, CV_8UC1);
    for (size_t i = 1; i < pts.size(); ++i)
        cv::line(out, pts[i - 1], pts[i], cv::Scalar(255), 2 * r, cv::LINE_AA);
    for (const cv::Point &p : pts)
        cv::circle(out, p, r, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat region;
    cv::compare(out, 127, region, cv::CMP_GT);
    return region;
}

// --------------------------------------------------------------------------- //
// Área pintada -> objeto
// --------------------------------------------------------------------------- //

std::vector<cv::Point2f> innerPoints(const cv::Mat &region, int n)
{
    const cv::Mat bin = binary(region);
    cv::Mat dist;
    cv::distanceTransform(bin, dist, cv::DIST_L2, 5);
    double maxDist = 0.0;
    cv::minMaxLoc(dist, nullptr, &maxDist);
    const float limit = 0.5f * static_cast<float>(maxDist);
    // np.nonzero já sai em ordem de linha (y, depois x) — a mesma do lexsort.
    std::vector<cv::Point> cand;
    for (int y = 0; y < dist.rows; ++y) {
        const float *row = dist.ptr<float>(y);
        for (int x = 0; x < dist.cols; ++x)
            if (row[x] >= limit)
                cand.emplace_back(x, y);
    }
    if (cand.empty())
        cv::findNonZero(bin, cand);
    std::vector<cv::Point2f> out;
    if (cand.empty() || n <= 0)
        return out;
    // np.linspace(0, len - 1, n).astype(int): passo em double, último = stop.
    const double stop = static_cast<double>(cand.size() - 1);
    const double step = n > 1 ? stop / (n - 1) : 0.0;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double v = (n > 1 && i == n - 1) ? stop : i * step;
        const cv::Point &p = cand[static_cast<size_t>(v)];
        out.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    return out;
}

double maskIou(const cv::Mat &a, const cv::Mat &b)
{
    const cv::Mat ba = binary(a), bb = binary(b);
    cv::Mat inter, uni;
    cv::bitwise_and(ba, bb, inter);
    cv::bitwise_or(ba, bb, uni);
    return static_cast<double>(cv::countNonZero(inter)) / std::max(static_cast<double>(cv::countNonZero(uni)), 1.0);
}

cv::Mat cleanMask(const cv::Mat &mask, double minFrac)
{
    // O upscale das máscaras de 256 px deixa pontilhado na borda.
    cv::Mat out = binary(mask);
    cv::Mat lab, stats, centroids;
    int n = cv::connectedComponentsWithStats(out, lab, stats, centroids, 8, CV_32S);
    if (n > 1) {
        int maxArea = 0;
        for (int i = 1; i < n; ++i)
            maxArea = std::max(maxArea, stats.at<int>(i, cv::CC_STAT_AREA));
        std::vector<uchar> keep(n, 0);
        for (int i = 1; i < n; ++i)
            keep[i] = stats.at<int>(i, cv::CC_STAT_AREA) >= maxArea * minFrac ? 255 : 0;
        for (int y = 0; y < out.rows; ++y) {
            const int *l = lab.ptr<int>(y);
            uchar *o = out.ptr<uchar>(y);
            for (int x = 0; x < out.cols; ++x)
                o[x] = keep[l[x]];
        }
    }
    cv::Mat holes;
    cv::compare(out, 0, holes, cv::CMP_EQ);
    n = cv::connectedComponentsWithStats(holes, lab, stats, centroids, 4, CV_32S);
    const int h = out.rows, w = out.cols;
    const double limit = std::max(cv::countNonZero(out) * 0.01, 16.0);
    std::vector<uchar> fill(n, 0);
    bool any = false;
    for (int i = 1; i < n; ++i) {
        const int x = stats.at<int>(i, cv::CC_STAT_LEFT), y = stats.at<int>(i, cv::CC_STAT_TOP);
        const int bw = stats.at<int>(i, cv::CC_STAT_WIDTH), bh = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (x > 0 && y > 0 && x + bw < w && y + bh < h && area < limit) {
            fill[i] = 1;
            any = true;
        }
    }
    if (any) {
        for (int y = 0; y < h; ++y) {
            const int *l = lab.ptr<int>(y);
            uchar *o = out.ptr<uchar>(y);
            for (int x = 0; x < w; ++x)
                if (fill[l[x]])
                    o[x] = 255;
        }
    }
    return out;
}

cv::Mat refine(const cv::Mat &rgb, const cv::Mat &mask, bool smart)
{
    cv::Mat m;
    binary(mask).convertTo(m, CV_32F, 1.0 / 255.0);
    const int side = std::max(mask.rows, mask.cols);
    if (smart) {
        cv::Mat rgbF;
        rgb.convertTo(rgbF, CV_32F, 1.0 / 255.0);
        // Janela ~ o erro do upscale da máscara (256 px -> lado da foto).
        m = guidedFilter(m, luma(rgbF), std::max(side / 160, 3), 1e-3);
    } else {
        const int k = std::max(side / 300, 1) * 2 + 1;
        cv::GaussianBlur(m, m, cv::Size(k, k), 0);
    }
    cv::Mat out = cv::max(m, 0.0);
    return cv::min(out, 1.0);
}

// --------------------------------------------------------------------------- //
// Segmenter
// --------------------------------------------------------------------------- //

#ifdef PHOTOEDITOR_HAVE_ONNX

namespace {

Q_LOGGING_CATEGORY(lcSegment, "photoeditor.imaging.segment")

const cv::Vec3f kMean(0.485f, 0.456f, 0.406f);
const cv::Vec3f kStd(0.229f, 0.224f, 0.225f);

Ort::Env &ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "photoeditor");
    return env;
}

std::basic_string<ORTCHAR_T> ortPath(const QString &path)
{
#ifdef _WIN32
    return QDir::toNativeSeparators(path).toStdWString();
#else
    return QFile::encodeName(path).toStdString();
#endif
}

std::vector<std::string> ioNames(Ort::Session &s, bool inputs)
{
    Ort::AllocatorWithDefaultOptions alloc;
    std::vector<std::string> names;
    const size_t n = inputs ? s.GetInputCount() : s.GetOutputCount();
    for (size_t i = 0; i < n; ++i)
        names.emplace_back(inputs ? s.GetInputNameAllocated(i, alloc).get() : s.GetOutputNameAllocated(i, alloc).get());
    return names;
}

std::vector<const char *> cstrs(const std::vector<std::string> &names)
{
    std::vector<const char *> out;
    for (const std::string &n : names)
        out.push_back(n.c_str());
    return out;
}

/// Saídas do encoder, pelo nome (o decoder as recebe com os mesmos nomes).
struct Embedding
{
    std::vector<std::string> names;
    std::vector<std::vector<int64_t>> shapes;
    std::vector<std::vector<float>> data;
};

} // namespace

struct Segmenter::Impl
{
    QString modelDir;
    bool hasModel = false;

    std::mutex mutex;
    bool failed = false;
    std::unique_ptr<Ort::Session> encoder;
    std::unique_ptr<Ort::Session> decoder;
    std::vector<std::string> encoderOut;
    std::vector<std::string> decoderIn;
    std::vector<std::string> decoderOut;
    std::list<std::pair<QString, std::shared_ptr<const Embedding>>> cache;  ///< LRU, recente no fim

    /// Sessões do encoder e do decoder, criadas na primeira seleção.
    bool load()
    {
        if (encoder)
            return true;
        if (failed || !hasModel)
            return false;
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(kSegmentThreads);
            opts.DisableCpuMemArena();
            const QDir dir(modelDir);
            encoder = std::make_unique<Ort::Session>(ortEnv(), ortPath(dir.filePath(QString::fromLatin1(kSegmentEncoder))).c_str(), opts);
            decoder = std::make_unique<Ort::Session>(ortEnv(), ortPath(dir.filePath(QString::fromLatin1(kSegmentDecoder))).c_str(), opts);
            encoderOut = ioNames(*encoder, false);
            decoderIn = ioNames(*decoder, true);
            decoderOut = ioNames(*decoder, false);
            qCInfo(lcSegment) << "Segmentation model loaded from" << modelDir;
            return true;
        } catch (const Ort::Exception &e) {
            qCWarning(lcSegment) << "Segmentation model failed to load:" << e.what();
            encoder.reset();
            decoder.reset();
            failed = true;
            return false;
        }
    }

    /// Embeddings da foto (chave = foto + versão do arquivo), com cache.
    std::shared_ptr<const Embedding> embed(const QString &key, const cv::Mat &rgb)
    {
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->first == key) {
                cache.splice(cache.end(), cache, it);
                return cache.back().second;
            }
        }
        if (!load())
            return nullptr;
        cv::Mat x;
        cv::resize(rgb, x, cv::Size(kSegmentInputSide, kSegmentInputSide), 0, 0, cv::INTER_AREA);
        constexpr int plane = kSegmentInputSide * kSegmentInputSide;
        std::vector<float> chw(3 * static_cast<size_t>(plane));
        for (int y = 0; y < kSegmentInputSide; ++y) {
            const cv::Vec3b *row = x.ptr<cv::Vec3b>(y);
            for (int xx = 0; xx < kSegmentInputSide; ++xx) {
                const size_t i = static_cast<size_t>(y) * kSegmentInputSide + xx;
                for (int c = 0; c < 3; ++c)
                    chw[c * static_cast<size_t>(plane) + i] = (static_cast<float>(row[xx][c]) / 255.0f - kMean[c]) / kStd[c];
            }
        }
        const std::array<int64_t, 4> shape{1, 3, kSegmentInputSide, kSegmentInputSide};
        const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(mem, chw.data(), chw.size(), shape.data(), shape.size());
        const char *inName = "pixel_values";
        const std::vector<const char *> outNames = cstrs(encoderOut);
        std::vector<Ort::Value> outs = encoder->Run(Ort::RunOptions{nullptr}, &inName, &input, 1, outNames.data(), outNames.size());
        auto emb = std::make_shared<Embedding>();
        for (size_t i = 0; i < outs.size(); ++i) {
            const auto info = outs[i].GetTensorTypeAndShapeInfo();
            const float *p = outs[i].GetTensorData<float>();
            emb->names.push_back(encoderOut[i]);
            emb->shapes.push_back(info.GetShape());
            emb->data.emplace_back(p, p + info.GetElementCount());
        }
        cache.emplace_back(key, emb);
        while (cache.size() > static_cast<size_t>(kSegmentEmbedCache))
            cache.pop_front();
        return emb;
    }

    /// Máscara do objeto sob uma área pintada conexa.
    cv::Mat object(const Embedding &emb, cv::Size size, const cv::Mat &region)
    {
        const int h = size.height, w = size.width;
        const float sx = static_cast<float>(static_cast<double>(kSegmentInputSide) / w);
        const float sy = static_cast<float>(static_cast<double>(kSegmentInputSide) / h);
        const cv::Rect bb = cv::boundingRect(region);
        const std::array<float, 4> box{static_cast<float>(bb.x) * sx, static_cast<float>(bb.y) * sy,
                                       static_cast<float>(bb.x + bb.width - 1) * sx,
                                       static_cast<float>(bb.y + bb.height - 1) * sy};
        struct Prompt
        {
            std::vector<cv::Point2f> points;
            bool box;
        };
        const std::vector<Prompt> prompts{{{}, true},
                                          {innerPoints(region, 1), false},
                                          {innerPoints(region, 3), false},
                                          {innerPoints(region, 3), true},
                                          {innerPoints(region, 5), true}};
        const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::vector<const char *> inNames = cstrs(decoderIn);
        const std::vector<const char *> outNames = cstrs(decoderOut);
        cv::Mat best = region;
        double bestScore = -1.0;
        // Tensores vazios ainda precisam de um ponteiro válido.
        float emptyF = 0.0f;
        int64_t emptyI = 0;
        for (const Prompt &pr : prompts) {
            const int64_t np = static_cast<int64_t>(pr.points.size());
            std::vector<float> pts;
            for (const cv::Point2f &p : pr.points) {
                pts.push_back(p.x * sx);
                pts.push_back(p.y * sy);
            }
            std::vector<int64_t> labels(static_cast<size_t>(np), 1);
            std::vector<float> boxes(box.begin(), box.end());
            const std::array<int64_t, 4> ptsShape{1, 1, np, 2};
            const std::array<int64_t, 3> labelsShape{1, 1, np};
            const std::array<int64_t, 3> boxShape{1, pr.box ? 1 : 0, 4};
            std::vector<Ort::Value> inputs;
            for (const std::string &name : decoderIn) {
                if (name == "input_points") {
                    inputs.push_back(Ort::Value::CreateTensor<float>(mem, np ? pts.data() : &emptyF, pts.size(),
                                                                     ptsShape.data(), ptsShape.size()));
                } else if (name == "input_labels") {
                    inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, np ? labels.data() : &emptyI, labels.size(),
                                                                       labelsShape.data(), labelsShape.size()));
                } else if (name == "input_boxes") {
                    inputs.push_back(Ort::Value::CreateTensor<float>(mem, boxes.data(), pr.box ? 4 : 0,
                                                                     boxShape.data(), boxShape.size()));
                } else {
                    const auto it = std::find(emb.names.begin(), emb.names.end(), name);
                    if (it == emb.names.end())
                        throw Ort::Exception("decoder input missing from the encoder: " + name, ORT_INVALID_ARGUMENT);
                    const size_t i = static_cast<size_t>(it - emb.names.begin());
                    auto &data = const_cast<std::vector<float> &>(emb.data[i]);
                    inputs.push_back(Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), emb.shapes[i].data(),
                                                                     emb.shapes[i].size()));
                }
            }
            // A segunda saída: pred_masks [1, 1, 3, 256, 256] (logits).
            std::vector<Ort::Value> outs = decoder->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(),
                                                        inputs.size(), &outNames[1], 1);
            const std::vector<int64_t> shape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
            const int count = static_cast<int>(shape[2]);
            const int mh = static_cast<int>(shape[3]), mw = static_cast<int>(shape[4]);
            float *data = outs[0].GetTensorMutableData<float>();
            for (int k = 0; k < count; ++k) {
                const cv::Mat logits(mh, mw, CV_32F, data + static_cast<size_t>(k) * mh * mw);
                cv::Mat up, mask;
                cv::resize(logits, up, cv::Size(w, h), 0, 0, cv::INTER_LINEAR);
                cv::compare(up, 0, mask, cv::CMP_GT);
                const double score = maskIou(mask, region);
                if (score > bestScore) {
                    best = mask;
                    bestScore = score;
                }
            }
        }
        return cleanMask(best);
    }
};

#else

struct Segmenter::Impl
{
    QString modelDir;
    bool hasModel = false;
};

#endif

Segmenter::Segmenter(const QString &modelDir)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->modelDir = modelDir;
    m_impl->hasModel = available(modelDir);
}

Segmenter::~Segmenter() = default;

bool Segmenter::available(const QString &modelDir)
{
#ifdef PHOTOEDITOR_HAVE_ONNX
    const QDir dir(modelDir);
    return !modelDir.isEmpty() && QFileInfo(dir.filePath(QString::fromLatin1(kSegmentEncoder))).isFile()
        && QFileInfo(dir.filePath(QString::fromLatin1(kSegmentDecoder))).isFile();
#else
    Q_UNUSED(modelDir);
    return false;
#endif
}

bool Segmenter::available() const
{
#ifdef PHOTOEDITOR_HAVE_ONNX
    std::lock_guard lock(m_impl->mutex);
    return m_impl->hasModel && !m_impl->failed;
#else
    return false;
#endif
}

bool Segmenter::warm(const QString &key, const cv::Mat &rgb)
{
#ifdef PHOTOEDITOR_HAVE_ONNX
    if (!m_impl->hasModel || rgb.empty())
        return false;
    std::lock_guard lock(m_impl->mutex);
    try {
        return m_impl->embed(key, rgb) != nullptr;
    } catch (const Ort::Exception &e) {
        qCWarning(lcSegment) << "Segmentation encoder failed:" << e.what();
        return false;
    }
#else
    Q_UNUSED(key);
    Q_UNUSED(rgb);
    return false;
#endif
}

cv::Mat Segmenter::select(const QString &key, const cv::Mat &rgb, const std::vector<cv::Point2d> &points,
                          double radius, bool smart)
{
    const cv::Mat region = paint(rgb.size(), points, radius);
    if (cv::countNonZero(region) == 0)
        return cv::Mat::zeros(rgb.size(), CV_32FC1);
#ifdef PHOTOEDITOR_HAVE_ONNX
    if (smart && m_impl->hasModel) {
        cv::Mat found;
        {
            std::lock_guard lock(m_impl->mutex);
            try {
                if (const auto emb = m_impl->embed(key, rgb)) {
                    cv::Mat lab;
                    const int n = cv::connectedComponents(region, lab, 8, CV_32S);
                    found = cv::Mat::zeros(region.size(), CV_8UC1);
                    for (int i = 1; i < n; ++i) {
                        cv::Mat part;
                        cv::compare(lab, i, part, cv::CMP_EQ);
                        cv::bitwise_or(found, m_impl->object(*emb, rgb.size(), part), found);
                    }
                }
            } catch (const Ort::Exception &e) {
                // Sem o modelo, o traço vale como pincel puro (o Python
                // levantaria a exceção; aqui a seleção não pode derrubar o app).
                qCWarning(lcSegment) << "Segmentation failed, using the painted area:" << e.what();
                found.release();
            }
        }
        if (!found.empty())
            return refine(rgb, found, true);
    }
#else
    Q_UNUSED(key);
    Q_UNUSED(smart);
#endif
    return refine(rgb, region, false);
}

} // namespace pe::imaging
