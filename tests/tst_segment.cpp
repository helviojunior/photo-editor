// SPDX-License-Identifier: BSD-2-Clause
// Seleção por pincel (SAM) e máscaras das camadas. As referências em
// data/segment/ saíram do segment.py (branch legacy), em Docker, com o mesmo
// traço: stroke_paint (área pintada), stroke_plain (pincel puro, sem SAM) e
// stroke_smart (objeto detectado pelo SAM).
#include "imaging/Develop.h"
#include "imaging/Mask.h"
#include "imaging/Segment.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

using namespace pe::imaging;

namespace {

const QString kData = QStringLiteral(PHOTOEDITOR_TEST_DATA);
const QString kModel = QStringLiteral(PHOTOEDITOR_MODELS_DIR) + QStringLiteral("/sam2.1-hiera-tiny");

// O mesmo traço do script de referência.
const std::vector<cv::Point2d> kStroke{{0.10, 0.30}, {0.12, 0.38}, {0.13, 0.46}};
constexpr double kRadius = 0.02;

cv::Mat readImage(const QString &path, int flags)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    const cv::Mat buf(1, static_cast<int>(data.size()), CV_8UC1, const_cast<char *>(data.constData()));
    return cv::imdecode(buf, flags);
}

cv::Mat sampleRgb()
{
    cv::Mat bgr = readImage(kData + QStringLiteral("/sample.jpg"), cv::IMREAD_COLOR);
    cv::Mat rgb;
    if (!bgr.empty())
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return rgb;
}

cv::Mat reference(const char *name)
{
    return readImage(kData + QStringLiteral("/segment/") + QLatin1String(name), cv::IMREAD_GRAYSCALE);
}

/// Máscara float [0, 1] -> u8 como o script de referência gravou.
cv::Mat toU8(const cv::Mat &f)
{
    cv::Mat u8(f.size(), CV_8UC1);
    for (int y = 0; y < f.rows; ++y)
        for (int x = 0; x < f.cols; ++x)
            u8.at<uchar>(y, x) = static_cast<uchar>(std::clamp(f.at<float>(y, x), 0.0f, 1.0f) * 255.0f + 0.5f);
    return u8;
}

cv::Mat over(const cv::Mat &m, double t)
{
    cv::Mat out;
    cv::compare(m, t, out, cv::CMP_GT);
    return out;
}

double maxAbsDiff(const cv::Mat &a, const cv::Mat &b)
{
    double mx = 0.0;
    cv::Mat d;
    cv::absdiff(a, b, d);
    cv::minMaxLoc(d, nullptr, &mx);
    return mx;
}

} // namespace

class SegmentTest : public QObject
{
    Q_OBJECT
private slots:
    void paintArea();
    void paintMatchesPython();
    void refinePlainMatchesPython();
    void innerPointsSpread();
    void iou();
    void clean();
    void selectFallback();
    void combine();
    void maskRoundtrip();
    void alpha();
    void samMatchesPython();
};

void SegmentTest::paintArea()
{
    const cv::Mat empty = paint(cv::Size(100, 50), {}, 0.1);
    QCOMPARE(empty.type(), CV_8UC1);
    QCOMPARE(cv::countNonZero(empty), 0);

    // Um ponto = um disco de raio round(0,1 * 100) = 10; 349 px no Python (o
    // círculo com antialiasing do OpenCV sai um pouco maior que pi * r²).
    const cv::Mat dot = paint(cv::Size(100, 50), {{0.5, 0.5}}, 0.1);
    QCOMPARE(dot.size(), cv::Size(100, 50));
    QCOMPARE(cv::countNonZero(dot), 349);
    QCOMPARE(dot.at<uchar>(25, 50), uchar(255));
    QCOMPARE(dot.at<uchar>(25, 70), uchar(0));

    // Raio mínimo de 1 px.
    QVERIFY(cv::countNonZero(paint(cv::Size(100, 50), {{0.5, 0.5}}, 0.0)) > 0);
}

void SegmentTest::paintMatchesPython()
{
    const cv::Mat rgb = sampleRgb();
    const cv::Mat ref = reference("stroke_paint.png");
    QVERIFY(!rgb.empty() && !ref.empty());
    const cv::Mat mine = paint(rgb.size(), kStroke, kRadius);
    QCOMPARE(mine.size(), ref.size());
    QVERIFY2(maskIou(mine, ref) > 0.999, qPrintable(QString::number(maskIou(mine, ref))));
}

void SegmentTest::refinePlainMatchesPython()
{
    const cv::Mat rgb = sampleRgb();
    const cv::Mat ref = reference("stroke_plain.png");
    QVERIFY(!rgb.empty() && !ref.empty());
    const cv::Mat soft = refine(rgb, paint(rgb.size(), kStroke, kRadius), false);
    QCOMPARE(soft.type(), CV_32FC1);
    QVERIFY(maxAbsDiff(toU8(soft), ref) <= 1.0);

    // Com o guia, a borda continua em [0, 1].
    const cv::Mat smart = refine(rgb, paint(rgb.size(), kStroke, kRadius), true);
    double mn = 0.0, mx = 0.0;
    cv::minMaxLoc(smart, &mn, &mx);
    QVERIFY(mn >= 0.0 && mx <= 1.0);
}

void SegmentTest::innerPointsSpread()
{
    cv::Mat region = cv::Mat::zeros(60, 80, CV_8UC1);
    region(cv::Rect(10, 20, 40, 20)).setTo(255);  // x 10..49, y 20..39
    const std::vector<cv::Point2f> one = innerPoints(region, 1);
    QCOMPARE(one.size(), size_t(1));
    const std::vector<cv::Point2f> five = innerPoints(region, 5);
    QCOMPARE(five.size(), size_t(5));
    for (const cv::Point2f &p : five) {
        QVERIFY(p.x >= 10 && p.x <= 49 && p.y >= 20 && p.y <= 39);
        // Miolo: longe da borda do retângulo.
        QVERIFY(p.y >= 24 && p.y <= 35);
    }
    // Em ordem de linha, o primeiro é o de cima e o último o de baixo.
    QCOMPARE(one.front(), five.front());
    QVERIFY(five.front().y <= five.back().y);
}

void SegmentTest::iou()
{
    cv::Mat a = cv::Mat::zeros(10, 10, CV_8UC1), b = a.clone();
    QCOMPARE(maskIou(a, b), 0.0);
    a(cv::Rect(0, 0, 4, 10)).setTo(255);
    b(cv::Rect(2, 0, 4, 10)).setTo(1);  // qualquer valor não nulo é "dentro"
    QCOMPARE(maskIou(a, b), 20.0 / 60.0);
    QCOMPARE(maskIou(a, a), 1.0);
}

void SegmentTest::clean()
{
    cv::Mat m = cv::Mat::zeros(100, 100, CV_8UC1);
    m(cv::Rect(10, 10, 60, 60)).setTo(255);  // 3600 px
    m(cv::Rect(90, 90, 5, 5)).setTo(255);    // 25 px < 2 %: ilha some
    m(cv::Rect(80, 10, 9, 9)).setTo(255);    // 81 px >= 72: fica
    m(cv::Rect(30, 30, 4, 4)).setTo(0);      // furo de 16 px < 37: tapado
    m(cv::Rect(40, 40, 10, 10)).setTo(0);    // furo de 100 px: fica
    m(cv::Rect(10, 50, 5, 5)).setTo(0);      // encosta no fundo pela borda? não: é o fundo
    const cv::Mat c = cleanMask(m);
    QCOMPARE(c.type(), CV_8UC1);
    QCOMPARE(c.at<uchar>(92, 92), uchar(0));
    QCOMPARE(c.at<uchar>(12, 82), uchar(255));
    QCOMPARE(c.at<uchar>(31, 31), uchar(255));
    QCOMPARE(c.at<uchar>(45, 45), uchar(0));
    QCOMPARE(c.at<uchar>(52, 12), uchar(0));

    // Furo que encosta na borda da imagem nunca é tapado.
    cv::Mat edge = cv::Mat::zeros(20, 20, CV_8UC1);
    edge(cv::Rect(0, 0, 20, 20)).setTo(255);
    edge(cv::Rect(0, 5, 2, 2)).setTo(0);
    QCOMPARE(cleanMask(edge).at<uchar>(5, 0), uchar(0));
}

void SegmentTest::selectFallback()
{
    const cv::Mat rgb = sampleRgb();
    QVERIFY(!rgb.empty());
    Segmenter seg(QStringLiteral("/nonexistent"));
    QVERIFY(!seg.available());
    QVERIFY(!Segmenter::available(QStringLiteral("/nonexistent")));
    QVERIFY(!seg.warm(QStringLiteral("k"), rgb));

    const cv::Mat none = seg.select(QStringLiteral("k"), rgb, {}, kRadius);
    QCOMPARE(none.type(), CV_32FC1);
    QCOMPARE(none.size(), rgb.size());
    QCOMPARE(cv::countNonZero(none), 0);

    // Sem modelo, smart cai para o pincel puro — o mesmo do Python com
    // smart=False.
    const cv::Mat plain = seg.select(QStringLiteral("k"), rgb, kStroke, kRadius, true);
    QVERIFY(maxAbsDiff(toU8(plain), reference("stroke_plain.png")) <= 1.0);
}

void SegmentTest::combine()
{
    cv::Mat region = cv::Mat::zeros(10, 10, CV_32FC1);
    region(cv::Rect(0, 0, 5, 10)).setTo(1.0);

    // Sem máscara atual: a própria área.
    cv::Mat add = combineStroke({}, region, StrokeMode::Add);
    QCOMPARE(add.type(), CV_32FC1);
    QCOMPARE(add.at<float>(0, 0), 1.0f);
    QCOMPARE(add.at<float>(0, 9), 0.0f);

    cv::Mat current = cv::Mat::zeros(10, 10, CV_8UC1);
    current(cv::Rect(3, 0, 7, 10)).setTo(255);
    add = combineStroke(current, region, StrokeMode::Add);
    QCOMPARE(cv::countNonZero(over(add, 0.5)), 100);

    const cv::Mat sub = combineStroke(current, region, StrokeMode::Subtract);
    QCOMPARE(sub.at<float>(0, 4), 0.0f);
    QCOMPARE(sub.at<float>(0, 7), 1.0f);
    QCOMPARE(cv::countNonZero(over(sub, 0.5)), 50);

    // Tirar tudo -> vazia; atual de outro tamanho conta como vazia.
    QVERIFY(combineStroke(current, cv::Mat::ones(10, 10, CV_32FC1), StrokeMode::Subtract).empty());
    QVERIFY(combineStroke(cv::Mat::ones(5, 5, CV_8UC1) * 255, region, StrokeMode::Subtract).empty());
    QVERIFY(combineStroke({}, cv::Mat::zeros(10, 10, CV_32FC1), StrokeMode::Add).empty());

    QCOMPARE(strokeModeByName(QStringLiteral("add")), std::optional(StrokeMode::Add));
    QCOMPARE(strokeModeByName(QStringLiteral("subtract")), std::optional(StrokeMode::Subtract));
    QVERIFY(!strokeModeByName(QStringLiteral("x")).has_value());
}

void SegmentTest::maskRoundtrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // Pasta com acento: caminhos não ASCII (Windows) passam pelo QFile.
    const QString dir = tmp.filePath(QStringLiteral("máscaras"));

    cv::Mat alpha(30, 40, CV_32FC1);
    for (int y = 0; y < alpha.rows; ++y)
        for (int x = 0; x < alpha.cols; ++x)
            alpha.at<float>(y, x) = (x - 5) / 30.0f;  // < 0 e > 1 nas pontas
    const QString key = saveMask(dir, alpha);
    QVERIFY(isMaskKey(key));
    QVERIFY(QFileInfo(maskPath(dir, key)).isFile());
    QCOMPARE(saveMask(dir, alpha), key);  // conteúdo igual, chave igual
    QCOMPARE(QDir(dir).entryList(QDir::Files).size(), 1);

    const cv::Mat loaded = loadMask(dir, key);
    QCOMPARE(loaded.type(), CV_8UC1);
    QCOMPARE(loaded.size(), alpha.size());
    QCOMPARE(maxAbsDiff(loaded, toU8(alpha)), 0.0);
    QCOMPARE(loaded.at<uchar>(0, 0), uchar(0));
    QCOMPARE(loaded.at<uchar>(0, 39), uchar(255));
    QCOMPARE(loaded.at<uchar>(0, 20), uchar(128));  // 0,5 * 255 + 0,5 truncado

    // A cópia é de quem chama: alterá-la não mexe no cache.
    cv::Mat changed = loadMask(dir, key);
    changed.setTo(7);
    QCOMPARE(loadMask(dir, key).at<uchar>(0, 39), uchar(255));

    QVERIFY(loadMask(dir, QStringLiteral("not-a-key")).empty());
    QVERIFY(loadMask(dir, QStringLiteral("0123456789abcdef0123")).empty());
    QCOMPARE(coverage(dir, QStringLiteral("0123456789abcdef0123")), 0.0);
    QVERIFY(overlayPng(dir, QStringLiteral("0123456789abcdef0123")).isEmpty());

    // > 127: colunas 20..39 de 40.
    QCOMPARE(coverage(dir, key), 0.5);

    const QByteArray png = overlayPng(dir, key);
    QVERIFY(png.startsWith("\x89PNG"));
    const cv::Mat buf(1, static_cast<int>(png.size()), CV_8UC1, const_cast<char *>(png.constData()));
    const cv::Mat rgba = cv::imdecode(buf, cv::IMREAD_UNCHANGED);
    QCOMPARE(rgba.channels(), 4);
    std::vector<cv::Mat> ch;
    cv::split(rgba, ch);
    QCOMPARE(cv::countNonZero(ch[0] != 255), 0);
    QCOMPARE(maxAbsDiff(ch[3], loaded), 0.0);

    // Traço sobre a máscara gravada.
    cv::Mat region = cv::Mat::zeros(30, 40, CV_32FC1);
    region(cv::Rect(0, 0, 10, 30)).setTo(1.0);
    const QString added = applyStroke(dir, key, region, StrokeMode::Add);
    QVERIFY(isMaskKey(added) && added != key);
    QCOMPARE(coverage(dir, added), 0.75);
    QVERIFY(applyStroke(dir, key, cv::Mat::ones(30, 40, CV_32FC1), StrokeMode::Subtract).isEmpty());
    QCOMPARE(applyStroke(dir, QString(), region, StrokeMode::Add),
             saveMask(dir, combineStroke({}, region, StrokeMode::Add)));
}

void SegmentTest::alpha()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    cv::Mat m = cv::Mat::zeros(50, 100, CV_32FC1);
    m(cv::Rect(50, 0, 50, 50)).setTo(1.0);
    const QString key = saveMask(tmp.path(), m);
    QVERIFY(isMaskKey(key));

    // Sem crop: só os tamanhos (preview 100x50 -> foto 200x100 -> saída 40x20).
    const cv::Mat a = layerAlpha(tmp.path(), key, cv::Size(200, 100), kCropIdentity, cv::Size(40, 20));
    QCOMPARE(a.type(), CV_32FC1);
    QCOMPARE(a.size(), cv::Size(40, 20));
    QCOMPARE(a.at<float>(10, 5), 0.0f);
    QCOMPARE(a.at<float>(10, 35), 1.0f);

    // Crop da metade direita: a camada ocupa tudo.
    Crop crop;
    crop.scale = 0.5;
    crop.cx = 0.75;
    crop.ratio = 0.5;  // quadro 1:2 (altura/largura) igual à foto
    const Crop norm = normalizeCrop(crop, 0.5);
    const cv::Mat right = layerAlpha(tmp.path(), key, cv::Size(200, 100), norm, cv::Size(20, 10));
    QCOMPARE(right.size(), cv::Size(20, 10));
    QVERIFY(cv::mean(right)[0] > 0.95);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("missing")));
    QVERIFY(layerAlpha(tmp.path(), QStringLiteral("0123456789abcdef0123"), cv::Size(200, 100), kCropIdentity,
                       cv::Size(40, 20))
                .empty());
}

void SegmentTest::samMatchesPython()
{
    if (!Segmenter::available(kModel))
        QSKIP("SAM model (or ONNX Runtime) not available");
    const cv::Mat rgb = sampleRgb();
    const cv::Mat ref = reference("stroke_smart.png");
    QVERIFY(!rgb.empty() && !ref.empty());

    Segmenter seg(kModel);
    QVERIFY(seg.available());
    QElapsedTimer timer;
    timer.start();
    const cv::Mat first = seg.select(QStringLiteral("sample-1"), rgb, kStroke, kRadius);
    const qint64 firstMs = timer.restart();
    const cv::Mat second = seg.select(QStringLiteral("sample-1"), rgb, kStroke, kRadius);
    const qint64 strokeMs = timer.elapsed();
    qInfo("SAM: first select (encoder + decoder) %lld ms, next stroke %lld ms", firstMs, strokeMs);

    QCOMPARE(first.type(), CV_32FC1);
    QCOMPARE(first.size(), rgb.size());
    QCOMPARE(maxAbsDiff(first, second), 0.0);  // embeddings do cache = mesmo resultado

    const double score = maskIou(over(first, 0.5), over(ref, 127));
    qInfo("SAM: IoU vs Python %.4f, mean |diff| %.3f/255", score, cv::mean([&] {
        cv::Mat d;
        cv::absdiff(toU8(first), ref, d);
        return d;
    }())[0]);
    QVERIFY2(score > 0.95, qPrintable(QString::number(score)));

    // O objeto vai além da área pintada (senão o SAM não fez nada).
    QVERIFY(cv::countNonZero(over(first, 0.5)) > cv::countNonZero(paint(rgb.size(), kStroke, kRadius)));
    QVERIFY(seg.warm(QStringLiteral("sample-1"), rgb));
}

QTEST_GUILESS_MAIN(SegmentTest)
#include "tst_segment.moc"
