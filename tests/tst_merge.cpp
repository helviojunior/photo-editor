// SPDX-License-Identifier: BSD-2-Clause
// Merge: compara com as referências da versão em Python
// (tests/data/merge/expected.json, gerado por tests/data/merge/gen_merge.py).
#include "imaging/Merge.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace pe::imaging;

namespace {

constexpr int kW = 547;
constexpr int kH = 365;

QJsonObject loadExpected()
{
    QFile f(QStringLiteral(PHOTOEDITOR_TEST_DATA "/merge/expected.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

qint64 checksum(const cv::Mat &m)
{
    const cv::Scalar s = cv::sum(m);
    return qint64(std::llround(s[0] + s[1] + s[2] + s[3]));
}

double fsum(const cv::Mat &m)
{
    const cv::Scalar s = cv::sum(m);
    return s[0] + s[1] + s[2] + s[3];
}

/// Mesma similaridade do gen_merge.py: 2 graus, 3% maior, deslocada (12, -7).
cv::Mat forward()
{
    cv::Mat m = cv::getRotationMatrix2D(cv::Point2f(kW / 2.f, kH / 2.f), 2.0, 1.03);
    m.at<double>(0, 2) += 12;
    m.at<double>(1, 2) -= 7;
    return m;
}

cv::Mat makeSrc(const cv::Mat &base, int num = 1, int den = 1)
{
    cv::Mat src;
    cv::warpAffine(base, src, forward(), cv::Size(kW, kH), cv::INTER_LINEAR, cv::BORDER_REFLECT);
    if (num != den) {
        cv::Mat lut(1, 256, CV_8U);
        for (int i = 0; i < 256; ++i)
            lut.at<uchar>(i) = static_cast<uchar>(std::min(255, i * num / den));
        cv::LUT(src, lut, src);
    }
    return src;
}

cv::Mat circleMask(int w, int h, cv::Point center, int radius)
{
    cv::Mat mask = cv::Mat::zeros(h, w, CV_8U);
    cv::circle(mask, center, radius, cv::Scalar(255), -1, cv::LINE_AA);
    return mask;
}

cv::Matx23d matrixOf(const QJsonValue &v)
{
    const QJsonArray rows = v.toArray();
    cv::Matx23d m;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 3; ++c)
            m(r, c) = rows.at(r).toArray().at(c).toDouble();
    }
    return m;
}

/// Maior distância (px) entre os cantos da imagem levados por ``a`` e por ``b``.
double cornerDistance(const cv::Matx23d &a, const cv::Matx23d &b)
{
    double worst = 0;
    for (const cv::Vec2d &p : {cv::Vec2d(0, 0), cv::Vec2d(kW, 0), cv::Vec2d(0, kH), cv::Vec2d(kW, kH)}) {
        const double dx = (a(0, 0) - b(0, 0)) * p[0] + (a(0, 1) - b(0, 1)) * p[1] + a(0, 2) - b(0, 2);
        const double dy = (a(1, 0) - b(1, 0)) * p[0] + (a(1, 1) - b(1, 1)) * p[1] + a(1, 2) - b(1, 2);
        worst = std::max(worst, std::hypot(dx, dy));
    }
    return worst;
}

void comparePiece(const std::optional<MergePiece> &piece, const QJsonValue &expected)
{
    if (expected.isNull()) {
        QVERIFY(!piece);
        return;
    }
    QVERIFY(piece.has_value());
    const QJsonObject e = expected.toObject();
    QCOMPARE(piece->origin.x, e.value("origin").toArray().at(0).toInt());
    QCOMPARE(piece->origin.y, e.value("origin").toArray().at(1).toInt());
    QCOMPARE(piece->alpha.cols, e.value("size").toArray().at(0).toInt());
    QCOMPARE(piece->alpha.rows, e.value("size").toArray().at(1).toInt());
    QCOMPARE(piece->rgb.size(), piece->alpha.size());
    const double rgbSum = fsum(piece->rgb), alphaSum = fsum(piece->alpha);
    qInfo("piece rgb %.4f/%.4f alpha %.5f/%.5f", rgbSum, e.value("rgb_sum").toDouble(), alphaSum,
          e.value("alpha_sum").toDouble());
    QVERIFY(std::abs(rgbSum - e.value("rgb_sum").toDouble()) <= 1e-4 * e.value("rgb_sum").toDouble());
    QVERIFY(std::abs(alphaSum - e.value("alpha_sum").toDouble()) <= 1e-4 * e.value("alpha_sum").toDouble());
}

} // namespace

class MergeTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_expected = loadExpected();
        QVERIFY(!m_expected.isEmpty());
        cv::Mat bgr = cv::imread(PHOTOEDITOR_TEST_DATA "/sample.jpg", cv::IMREAD_COLOR);
        QVERIFY(!bgr.empty());
        cv::cvtColor(bgr, m_base, cv::COLOR_BGR2RGB);
        m_src = makeSrc(m_base);
        m_ball = circleMask(kW, kH, {300, 180}, 40);
        // As entradas têm de ser as mesmas do Python, pixel a pixel.
        QCOMPARE(checksum(m_base), qint64(m_expected.value("sample_checksum").toDouble()));
        QCOMPARE(checksum(m_src), qint64(m_expected.value("src_checksum").toDouble()));
        QCOMPARE(checksum(makeSrc(m_base, 9, 10)), qint64(m_expected.value("dark_checksum").toDouble()));
        QCOMPARE(checksum(m_ball), qint64(m_expected.value("ball_checksum").toDouble()));
    }

    void align_data()
    {
        QTest::addColumn<QString>("key");
        QTest::newRow("plain") << "align";
        QTest::newRow("dark") << "align_dark";
        QTest::newRow("exclude") << "align_exclude";
    }

    void align()
    {
        QFETCH(QString, key);
        const QJsonObject e = m_expected.value(key).toObject();
        const cv::Mat src = key == QLatin1String("align_dark") ? makeSrc(m_base, 9, 10) : m_src;
        const cv::Mat exclude = key == QLatin1String("align_exclude") ? m_ball : cv::Mat();
        const MergeAlign a = pe::imaging::align(m_base, src, exclude);
        QVERIFY(a.ok);
        QVERIFY(e.value("ok").toBool());

        const cv::Matx23d want = matrixOf(e.value("matrix"));
        const double vsPython = cornerDistance(a.matrix, want);
        const double vsTruth = cornerDistance(a.matrix, matrixOf(m_expected.value("expected_matrix")));
        const MergeDescription d = describe(a.matrix), dw = describe(want);
        qInfo("%s: inliers %d/%d error %.3f/%.3f gain %.4f,%.4f,%.4f/%s corner diff vs Python %.3f px, vs truth %.3f px; "
              "angle %.4f/%.4f scale %.5f/%.5f",
              qPrintable(key), a.inliers, e.value("inliers").toInt(), *a.error, e.value("error").toDouble(), a.gain[0],
              a.gain[1], a.gain[2], QJsonDocument(e.value("gain").toArray()).toJson(QJsonDocument::Compact).constData(),
              vsPython, vsTruth, d.angle, dw.angle, d.scale, dw.scale);
        QVERIFY(vsPython < 1.0);
        QVERIFY(vsTruth < 1.0);
        QVERIFY(std::abs(d.angle - dw.angle) < 0.02);
        QVERIFY(std::abs(d.scale - dw.scale) < 1e-3);
        const int inl = e.value("inliers").toInt();
        QVERIFY(std::abs(a.inliers - inl) <= inl / 5);
        QVERIFY(std::abs(*a.error - e.value("error").toDouble()) < 0.05);
        for (int c = 0; c < 3; ++c)
            QVERIFY(std::abs(a.gain[c] - e.value("gain").toArray().at(c).toDouble()) < 0.005);
    }

    void alignFails()
    {
        const MergeAlign a = pe::imaging::align(m_base, cv::Mat(200, 300, CV_8UC3, cv::Scalar::all(90)));
        const QJsonObject e = m_expected.value("align_flat").toObject();
        QVERIFY(!a.ok);
        QCOMPARE(a.inliers, 0);
        QVERIFY(!a.error.has_value());
        QCOMPARE(a.matrix, matrixOf(e.value("matrix")));
        QCOMPARE(a.gain, cv::Vec3d(1, 1, 1));
        // O JSON gravado e o hash do Python.
        QCOMPARE(mergeAlignToJson(a), e);
        QCOMPARE(alignVersion(mergeAlignToJson(a)), m_expected.value("align_version").toString());
        const MergeAlign back = mergeAlignFromJson(e);
        QCOMPARE(back.matrix, a.matrix);
        QCOMPARE(back.ok, false);
    }

    void gainFixed()
    {
        cv::Mat inv;
        cv::invertAffineTransform(forward(), inv);
        const cv::Vec3d g = alignGain(m_base, m_src, cv::Matx23d(inv.ptr<double>()));
        const QJsonArray e = m_expected.value("gain_fixed").toArray();
        for (int c = 0; c < 3; ++c)
            QCOMPARE(g[c], e.at(c).toDouble());
    }

    void describeAndScale()
    {
        const cv::Matx23d m(1.01, -0.02, 5.5, 0.02, 1.01, -3.25);
        const QJsonObject e = m_expected.value("describe").toObject();
        QCOMPARE(describeToJson(m), e);
        const cv::Matx23d s = scaledMatrix(m, {kW, kH}, {1094, 730}, {kW, kH}, {2000, 1333});
        const cv::Matx23d want = matrixOf(m_expected.value("scaled"));
        for (int i = 0; i < 6; ++i)
            QVERIFY(std::abs(s.val[i] - want.val[i]) < 1e-12);
    }

    void cutoutBackdropPaste()
    {
        const cv::Matx23d m(1.01, -0.02, 5.5, 0.02, 1.01, -3.25);
        const cv::Vec3d gain(1.1, 0.95, 1.0);
        // astype(float32) / 255.0: divisão em float, como o NumPy.
        cv::Mat alpha(m_ball.size(), CV_32F);
        for (int r = 0; r < alpha.rows; ++r) {
            for (int c = 0; c < alpha.cols; ++c)
                alpha.at<float>(r, c) = static_cast<float>(m_ball.at<uchar>(r, c)) / 255.f;
        }

        const auto piece = cutout(m_src, alpha, m, gain, {kW, kH});
        comparePiece(piece, m_expected.value("cutout"));
        const cv::Rect crop(250, 130, 120, 110);
        const auto pieceOff = cutout(m_src(crop).clone(), alpha(crop).clone(), m, gain, {kW, kH}, crop.tl());
        comparePiece(pieceOff, m_expected.value("cutout_offset"));
        comparePiece(cutout(m_src, alpha, cv::Matx23d(1, 0, 2000, 0, 1, 0), gain, {kW, kH}),
                     m_expected.value("cutout_outside"));
        QVERIFY(!cutout(m_src, cv::Mat::zeros(kH, kW, CV_32F), m, gain, {kW, kH}));

        cv::Mat canvas = m_base.clone();
        paste(canvas, *piece, 0.6);
        const qint64 pasteSum = checksum(canvas);
        qInfo("paste %lld/%lld", pasteSum, qint64(m_expected.value("paste_checksum").toDouble()));
        QVERIFY(std::abs(pasteSum - qint64(m_expected.value("paste_checksum").toDouble())) <= 20);
        const cv::Vec3b px = canvas.at<cv::Vec3b>(180, 305);
        const QJsonArray epx = m_expected.value("paste_pixel").toArray();
        for (int c = 0; c < 3; ++c)
            QVERIFY(std::abs(px[c] - epx.at(c).toInt()) <= 1);

        const cv::Mat rgba = pieceToRgba(piece, {kW, kH});
        QCOMPARE(rgba.type(), CV_8UC4);
        cv::Mat ch[4];
        cv::split(rgba, ch);
        qInfo("rgba %lld/%lld opaque %d/%d", checksum(rgba), qint64(m_expected.value("rgba_checksum").toDouble()),
              cv::countNonZero(ch[3]), m_expected.value("rgba_opaque").toInt());
        QVERIFY(std::abs(checksum(rgba) - qint64(m_expected.value("rgba_checksum").toDouble())) <= 20);
        QCOMPARE(cv::countNonZero(ch[3]), m_expected.value("rgba_opaque").toInt());
        QCOMPARE(checksum(pieceToRgba(std::nullopt, {10, 10})), qint64(0));

        // _mask_region e backdrop
        const auto region = maskRegion(m_ball, {kW, kH});
        QVERIFY(region.has_value());
        const QJsonObject er = m_expected.value("mask_region").toObject();
        QCOMPARE(region->second, cv::Point(er.value("origin").toArray().at(0).toInt(),
                                           er.value("origin").toArray().at(1).toInt()));
        QCOMPARE(region->first.cols, er.value("size").toArray().at(0).toInt());
        QCOMPARE(region->first.rows, er.value("size").toArray().at(1).toInt());
        QVERIFY(std::abs(fsum(region->first) - er.value("sum").toDouble()) < 1e-2);
        const auto region2 = maskRegion(m_ball, {1094, 730});
        const QJsonObject er2 = m_expected.value("mask_region_2x").toObject();
        QCOMPARE(region2->second, cv::Point(er2.value("origin").toArray().at(0).toInt(),
                                            er2.value("origin").toArray().at(1).toInt()));
        QCOMPARE(region2->first.cols, er2.value("size").toArray().at(0).toInt());
        QVERIFY(std::abs(fsum(region2->first) - er2.value("sum").toDouble()) < 1e-2);
        QVERIFY(!maskRegion(cv::Mat::zeros(10, 10, CV_8U), {20, 20}));

        const MergePiece bd = backdrop(m_src, region->first, region->second, m, gain);
        comparePiece(bd, m_expected.value("backdrop"));
        canvas = m_base.clone();
        paste(canvas, bd, 0.75);
        QVERIFY(std::abs(checksum(canvas) - qint64(m_expected.value("backdrop_paste_checksum").toDouble())) <= 20);
    }

    void serviceHelpers()
    {
        const QJsonObject eo = m_expected.value("default_opacities").toObject();
        for (int n = 0; n <= 6; ++n) {
            const std::vector<double> got = defaultOpacities(n);
            const QJsonArray want = eo.value(QString::number(n)).toArray();
            QCOMPARE(int(got.size()), int(want.size()));
            for (int i = 0; i < want.size(); ++i)
                QCOMPARE(got[i], want.at(i).toDouble());
        }

        const cv::Matx23d m(1.01, -0.02, 5.5, 0.02, 1.01, -3.25);
        QCOMPARE(alignVersion(m, cv::Vec3d(1.1, 0.95, 1.0)), m_expected.value("align_version_fixed").toString());
        QCOMPARE(alignVersion(std::nullopt, std::nullopt), m_expected.value("align_version_empty").toString());
        QCOMPARE(alignVersion(QJsonObject{}), m_expected.value("align_version_empty").toString());

        const QJsonObject en = m_expected.value("export_name").toObject();
        for (auto it = en.begin(); it != en.end(); ++it)
            QCOMPARE(mergeExportName(it.key()), it.value().toString());

        const cv::Mat small = circleMask(kW, kH, {100, 100}, 20);
        const QJsonObject ec = m_expected.value("clears").toObject();
        QCOMPARE(layerClearsBaseArea(m_ball, small, m), ec.value("near").toBool());
        QCOMPARE(layerClearsBaseArea(m_ball, small, cv::Matx23d(1, 0, 0, 0, 1, 0)), ec.value("far").toBool());
        QCOMPARE(layerClearsBaseArea(m_ball, small, cv::Matx23d(1, 0, 200, 0, 1, 80)), ec.value("onto").toBool());
        QVERIFY(!layerClearsBaseArea(m_ball, cv::Mat::zeros(kH, kW, CV_8U), m));
    }

private:
    QJsonObject m_expected;
    cv::Mat m_base;
    cv::Mat m_src;
    cv::Mat m_ball;
};

QTEST_GUILESS_MAIN(MergeTest)
#include "tst_merge.moc"
