// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Crop.h"
#include "imaging/Develop.h"
#include "imaging/SettingsHash.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace pe::imaging;

namespace {

const QString kData = QStringLiteral(PHOTOEDITOR_TEST_DATA "/develop/");

cv::Mat loadRgb(const QString &name)
{
    cv::Mat bgr = cv::imread((kData + name).toStdString(), cv::IMREAD_COLOR);
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return rgb;
}

QJsonObject cases()
{
    QFile f(kData + QStringLiteral("cases.json"));
    f.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(f.readAll()).object();
}

/// Maior diferença e fração de pixels com diferença > 1 entre duas imagens.
std::pair<int, double> compare(const cv::Mat &a, const cv::Mat &b)
{
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    double maxVal = 0;
    cv::minMaxLoc(diff.reshape(1), nullptr, &maxVal);
    cv::Mat over = diff.reshape(1) > 1;
    return {static_cast<int>(maxVal), static_cast<double>(cv::countNonZero(over)) / over.total()};
}

} // namespace

class DevelopTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizeMatchesPython()
    {
        const QJsonObject n = cases().value(QStringLiteral("_normalize")).toObject();
        const Values v = valuesFromJson(n.value(QStringLiteral("in")).toObject());
        const QJsonObject expected = n.value(QStringLiteral("out")).toObject();
        for (int i = 0; i < SliderCount; ++i)
            QCOMPARE(v[i], expected.value(QLatin1String(sliders()[i].name)).toDouble());
    }

    void pyFloatRepr()
    {
        QCOMPARE(pyFloat(0.5), QStringLiteral("0.5"));
        QCOMPARE(pyFloat(1.0), QStringLiteral("1.0"));
        QCOMPARE(pyFloat(-45.0), QStringLiteral("-45.0"));
        QCOMPARE(pyFloat(0.35), QStringLiteral("0.35"));
        QCOMPARE(pyFloat(1e-05), QStringLiteral("1e-05"));
        QCOMPARE(pyFloat(0.0001), QStringLiteral("0.0001"));
        QCOMPARE(pyFloat(1.5e16), QStringLiteral("1.5e+16"));
        QCOMPARE(pyFloat(123456.789), QStringLiteral("123456.789"));
        QCOMPARE(pyFloat(0.1 + 0.2), QStringLiteral("0.30000000000000004"));
    }

    void settingsHashMatchesPython_data()
    {
        QTest::addColumn<QJsonObject>("item");
        for (const auto &v : cases().value(QStringLiteral("_hashes")).toArray()) {
            const QJsonObject o = v.toObject();
            QTest::newRow(o.value(QStringLiteral("name")).toString().toUtf8()) << o;
        }
    }

    void settingsHashMatchesPython()
    {
        QFETCH(QJsonObject, item);
        const Values values = valuesFromJson(item.value(QStringLiteral("values")).toObject());
        const QString preset = item.value(QStringLiteral("preset")).toString();
        const Crop crop = item.value(QStringLiteral("crop")).isObject()
            ? cropFromJson(item.value(QStringLiteral("crop")).toObject())
            : kCropIdentity;
        const auto layers = normalizeLayers(item.value(QStringLiteral("layers")).toArray());
        // Sem a chave exposure o Python a deixava int 0 (o caso "nunca editada").
        const bool pristine = !item.value(QStringLiteral("values")).toObject().contains(QStringLiteral("exposure"));
        QCOMPARE(settingsHash(values, preset, crop, layers, pristine), item.value(QStringLiteral("hash")).toString());
    }

    void normalizeCropMatchesPython()
    {
        const QJsonObject all = cases();
        for (const auto &[key, ratios] :
             {std::pair{QStringLiteral("_crop"), std::vector<double>{}},
              std::pair{QStringLiteral("_crop90"), std::vector<double>{}},
              std::pair{QStringLiteral("_instagram"), instagramRatioValues()}}) {
            const QJsonObject c = all.value(key).toObject();
            // _crop foi normalizado com a proporção da foto original; os demais, com a da reduzida.
            const cv::Mat img = loadRgb(QStringLiteral("input.png"));
            const double aspect = c.contains(QStringLiteral("aspect")) ? c.value(QStringLiteral("aspect")).toDouble()
                                                                      : static_cast<double>(img.rows) / img.cols;
            const Crop got = normalizeCrop(cropFromJson(c.value(QStringLiteral("in")).toObject()), aspect, ratios);
            const Crop want = cropFromJson(c.value(QStringLiteral("out")).toObject());
            QVERIFY2(got == want, qPrintable(QStringLiteral("%1: got %2 %3 %4 %5 %6").arg(key).arg(got.scale).arg(got.cx)
                                                 .arg(got.cy).arg(got.angle).arg(got.ratio)));
        }
    }

    void cropPixelsMatchPython()
    {
        const cv::Mat img = loadRgb(QStringLiteral("input.png"));
        const QJsonObject all = cases();
        for (const auto &[key, file] : {std::pair{QStringLiteral("_crop"), QStringLiteral("crop.png")},
                                        std::pair{QStringLiteral("_crop90"), QStringLiteral("crop90.png")}}) {
            const Crop crop = cropFromJson(all.value(key).toObject().value(QStringLiteral("out")).toObject());
            const cv::Mat got = applyCrop(img, crop);
            const cv::Mat want = loadRgb(file);
            QCOMPARE(got.size(), want.size());
            const auto [maxDiff, frac] = compare(got, want);
            QVERIFY2(maxDiff <= 2, qPrintable(QStringLiteral("%1: max diff %2").arg(key).arg(maxDiff)));
        }
    }

    void renderMatchesPython_data()
    {
        QTest::addColumn<QString>("name");
        for (const auto &key : cases().keys()) {
            if (!key.startsWith(u'_'))
                QTest::newRow(key.toUtf8()) << key;
        }
    }

    void renderMatchesPython()
    {
        QFETCH(QString, name);
        const QJsonObject c = cases().value(name).toObject();
        const cv::Mat img = loadRgb(QStringLiteral("input.png"));
        const cv::Mat got = render(img, valuesFromJson(c.value(QStringLiteral("values")).toObject()),
                                   c.value(QStringLiteral("preset")).toString());
        const cv::Mat want = loadRgb(name + QStringLiteral(".png"));
        const auto [maxDiff, frac] = compare(got, want);
        // float32 do NumPy x float do C++: no máximo 1 nível, quase sempre 0.
        QVERIFY2(maxDiff <= 2 && frac < 0.001,
                 qPrintable(QStringLiteral("max diff %1, %2% > 1").arg(maxDiff).arg(frac * 100)));
    }

    void neutralIsIdentity()
    {
        const cv::Mat img = loadRgb(QStringLiteral("input.png"));
        const cv::Mat out = render(img, kNeutral);
        QCOMPARE(compare(img, out).first, 0);
    }

    void wbRoundTrip()
    {
        const auto [t, m] = gainsToWb(cv::Vec3d(wbGains(37, -21)));
        QVERIFY(std::abs(t - 37) < 1e-3);
        QVERIFY(std::abs(m + 21) < 1e-3);
    }
};

QTEST_GUILESS_MAIN(DevelopTest)
#include "tst_develop.moc"
