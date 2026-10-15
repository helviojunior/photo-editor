// SPDX-License-Identifier: BSD-2-Clause
// Auto: compara com as referências da versão em Python
// (tests/data/auto/expected.json, gerado por tests/data/auto/gen_auto.py).
#include "imaging/Auto.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <functional>

using namespace pe::imaging;

namespace {

QJsonObject loadExpected()
{
    QFile f(QStringLiteral(PHOTOEDITOR_TEST_DATA "/auto/expected.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

cv::Mat loadSample()
{
    cv::Mat bgr = cv::imread(PHOTOEDITOR_TEST_DATA "/sample.jpg", cv::IMREAD_COLOR);
    cv::Mat rgb;
    if (!bgr.empty())
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return rgb;
}

using Fn = std::function<int(int)>;

/// A MESMA tabela do gen_auto.py: LUT inteira por canal e escala.
struct Variant
{
    const char *name;
    Fn r, g, b;
    int scale;
};

const std::vector<Variant> &variants()
{
    static const std::vector<Variant> kVariants{
        {"original", [](int i) { return i; }, [](int i) { return i; }, [](int i) { return i; }, 1},
        {"dark", [](int i) { return i * 2 / 5; }, [](int i) { return i * 2 / 5; }, [](int i) { return i * 2 / 5; }, 1},
        {"bright", [](int i) { return i * 8 / 5; }, [](int i) { return i * 8 / 5; }, [](int i) { return i * 8 / 5; }, 1},
        {"warm", [](int i) { return i * 6 / 5; }, [](int i) { return i; }, [](int i) { return i * 3 / 4; }, 1},
        {"flat", [](int i) { return 96 + i * 64 / 255; }, [](int i) { return 96 + i * 64 / 255; },
         [](int i) { return 96 + i * 64 / 255; }, 1},
        {"large", [](int i) { return i; }, [](int i) { return i; }, [](int i) { return i; }, 2},
    };
    return kVariants;
}

cv::Mat makeVariant(const cv::Mat &rgb, const Variant &v)
{
    cv::Mat lut(1, 256, CV_8UC3);
    for (int i = 0; i < 256; ++i) {
        lut.at<cv::Vec3b>(0, i) = cv::Vec3b(cv::saturate_cast<uchar>(v.r(i)), cv::saturate_cast<uchar>(v.g(i)),
                                            cv::saturate_cast<uchar>(v.b(i)));
    }
    cv::Mat out;
    cv::LUT(rgb, lut, out);
    if (v.scale != 1)
        cv::resize(out, out, cv::Size(out.cols * v.scale, out.rows * v.scale), 0, 0, cv::INTER_NEAREST);
    return out;
}

/// A miniatura em float que o auto_values analisa.
cv::Mat analysisImage(const cv::Mat &img)
{
    const double s = static_cast<double>(kAutoAnalysisSide) / std::max(img.rows, img.cols);
    cv::Mat small = img;
    if (s < 1) {
        cv::resize(img, small, cv::Size(std::max(int(img.cols * s), 1), std::max(int(img.rows * s), 1)), 0, 0,
                   cv::INTER_AREA);
    }
    cv::Mat x;
    small.convertTo(x, CV_32FC3, 1.0 / 255.0);
    return x;
}

} // namespace

class AutoTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_expected = loadExpected();
        QVERIFY(!m_expected.isEmpty());
        m_sample = loadSample();
        QVERIFY(!m_sample.empty());
        // O decodificador JPEG precisa dar os mesmos pixels do cv2 do Python.
        const cv::Scalar sum = cv::sum(m_sample);
        QCOMPARE(qint64(sum[0] + sum[1] + sum[2]), qint64(m_expected.value("sample_checksum").toDouble()));
    }

    void matchesPython_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < int(variants().size()); ++i)
            QTest::newRow(variants()[i].name) << i;
    }

    void matchesPython()
    {
        QFETCH(int, index);
        const Variant &v = variants()[index];
        const QJsonObject exp = m_expected.value("cases").toObject().value(v.name).toObject();
        QVERIFY(!exp.isEmpty());
        const cv::Mat img = makeVariant(m_sample, v);
        const cv::Mat x = analysisImage(img);

        // Features
        const AutoFeatures f = autoFeatures(x);
        const QJsonObject ef = exp.value("features").toObject();
        qInfo("%s: L %.5f/%.5f dr %.5f/%.5f clip %.5f/%.5f bg %.5f/%.5f rb %.5f/%s", v.name, f.L,
              ef.value("L").toDouble(), f.dr, ef.value("dr").toDouble(), f.clip, ef.value("clip").toDouble(),
              f.subjectBg, ef.value("subject_bg").toDouble(), f.whitesRb,
              qPrintable(ef.value("whites_rb").isNull() ? QStringLiteral("nan")
                                                        : QString::number(ef.value("whites_rb").toDouble())));
        QVERIFY(std::abs(f.L - ef.value("L").toDouble()) < 0.05);
        QVERIFY(std::abs(f.dr - ef.value("dr").toDouble()) < 0.005);
        QVERIFY(std::abs(f.clip - ef.value("clip").toDouble()) < 0.002);
        QVERIFY(std::abs(f.subjectBg - ef.value("subject_bg").toDouble()) < 0.05);
        if (ef.value("whites_rb").isNull())
            QVERIFY(std::isnan(f.whitesRb));
        else
            QVERIFY(std::abs(f.whitesRb - ef.value("whites_rb").toDouble()) < 0.005);

        // Máscaras: contagem de pixels (o Python soma booleanos).
        const auto near = [](int got, double want, double rel) {
            return std::abs(got - want) <= std::max(rel * want, 50.0);
        };
        const int hog = cv::countNonZero(hogBoxes(x));
        const int tex = cv::countNonZero(textureMask(x));
        const int sub = cv::countNonZero(subjectRegion(x));
        qInfo("%s: hog %d/%g texture %d/%g subject %d/%g", v.name, hog, exp.value("hog_pixels").toDouble(), tex,
              exp.value("texture_pixels").toDouble(), sub, exp.value("subject_pixels").toDouble());
        QVERIFY(near(hog, exp.value("hog_pixels").toDouble(), 0.01));
        QVERIFY(near(tex, exp.value("texture_pixels").toDouble(), 0.01));
        QVERIFY(near(sub, exp.value("subject_pixels").toDouble(), 0.01));

        // Iluminante
        const cv::Vec3f illum = whitePatchNeutral(x);
        const QJsonArray ei = exp.value("illum").toArray();
        for (int c = 0; c < 3; ++c)
            QVERIFY(std::abs(illum[c] - ei.at(c).toDouble()) < 1e-3);

        // Pertinências e parâmetros
        const auto w = autoMemberships(f);
        const QJsonObject ew = exp.value("memberships").toObject();
        for (int c = 0; c < AutoCaseCount; ++c) {
            const QString name = autoCaseName(static_cast<AutoCase>(c));
            QVERIFY2(std::abs(w[c] - ew.value(name).toDouble()) < 0.02, qPrintable(name));
        }
        const auto [params, dominant] = autoCaseParams(x);
        const QJsonObject ep = exp.value("params").toObject();
        QVERIFY(std::abs(params.target - ep.value("target").toDouble()) < 0.005);
        QVERIFY(std::abs(params.shadow - ep.value("shadow").toDouble()) < 0.02);
        QVERIFY(std::abs(params.wbStrength - ep.value("wb_strength").toDouble()) < 1e-9);
        QVERIFY(std::abs(params.contrast - ep.value("contrast").toDouble()) < 0.005);
        QCOMPARE(autoCaseName(dominant), exp.value("case").toString());

        // Valores dos sliders
        const AutoResult r = autoValues(img);
        QCOMPARE(r.caseName, exp.value("case").toString());
        const QJsonObject ev = exp.value("values").toObject();
        QString got;
        for (int i = 0; i < SliderCount; ++i) {
            const auto s = static_cast<Slider>(i);
            got += QStringLiteral("%1=%2/%3 ").arg(QLatin1String(sliders()[i].name)).arg(r.values[i])
                       .arg(ev.value(QLatin1String(sliders()[i].name)).toDouble());
            if (!isAutoSlider(s)) {
                QCOMPARE(r.values[i], 0.0);
                continue;
            }
            const double want = ev.value(QLatin1String(sliders()[i].name)).toDouble();
            const double tol = s == Exposure ? 0.1 : 2.0;
            QVERIFY2(std::abs(r.values[i] - want) <= tol + 1e-9, qPrintable(got));
        }
        qInfo("%s: %s", v.name, qPrintable(got));
    }

    void applyKeepsColor()
    {
        Values current = kNeutral;
        current[Vibrance] = 30;
        current[Saturation] = -10;
        current[Highlights] = 40;
        current[Exposure] = -1;
        AutoResult r;
        r.values[Exposure] = 0.5;
        r.values[Temperature] = 12;
        const Values out = applyAuto(current, r);
        QCOMPARE(out[Vibrance], 30.0);
        QCOMPARE(out[Saturation], -10.0);
        // highlights/whites/blacks voltam ao neutro (o Auto os devolve zerados).
        QCOMPARE(out[Highlights], 0.0);
        QCOMPARE(out[Exposure], 0.5);
        QCOMPARE(out[Temperature], 12.0);
    }

    void maskBox()
    {
        cv::Mat m = cv::Mat::zeros(50, 80, CV_8U);
        QVERIFY(autoMaskBox(m).empty());
        m(cv::Rect(10, 5, 20, 7)).setTo(200);
        m.at<uchar>(40, 70) = 127;  // não passa do limiar
        QCOMPARE(autoMaskBox(m), cv::Rect(10, 5, 20, 7));
    }

    void tinyImage()
    {
        // Imagem menor que a janela do HOG: não pode lançar.
        const cv::Mat tiny(20, 30, CV_8UC3, cv::Scalar(120, 110, 100));
        const AutoResult r = autoValues(tiny);
        QVERIFY(!r.caseName.isEmpty());
    }

private:
    QJsonObject m_expected;
    cv::Mat m_sample;
};

QTEST_GUILESS_MAIN(AutoTest)
#include "tst_auto.moc"
