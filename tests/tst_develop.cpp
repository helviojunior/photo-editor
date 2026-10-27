// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Crop.h"
#include "imaging/Develop.h"
#include "imaging/Look.h"
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

/// Todos os 256 níveis de cinza + rampas com os canais trocados (3 linhas x 256).
cv::Mat ramps()
{
    cv::Mat img(4, 256, CV_8UC3);
    for (int i = 0; i < 256; ++i) {
        img.at<cv::Vec3b>(0, i) = cv::Vec3b(i, i, i);
        img.at<cv::Vec3b>(1, i) = cv::Vec3b(i, 255 - i, (i * 7) % 256);
        img.at<cv::Vec3b>(2, i) = cv::Vec3b((i * 3) % 256, i, 128);
        img.at<cv::Vec3b>(3, i) = cv::Vec3b(200, (i * 5) % 256, i);
    }
    return img;
}

/// Níveis feitos à mão, na escala 0..255 (o que o Photoshop faz).
double handLevels(double x, int inB, double gamma, int inW, int outB, int outW)
{
    double v = std::clamp((x - inB) / (inW - inB), 0.0, 1.0);
    v = std::pow(v, 1.0 / gamma);
    return outB + v * (outW - outB);
}

bool sameImage(const cv::Mat &a, const cv::Mat &b)
{
    return a.size() == b.size() && a.type() == b.type() && compare(a, b).first == 0;
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

    // Um quarto de volta (90°/270°) troca retrato/paisagem: num ratio fixo do
    // Instagram o quadro invertido cabe MAIOR que a referência a 0°. A versão
    // Python/JS travava em 1.0 e não deixava o quadro crescer — aqui não.
    void cropMaxScaleQuarterTurnFillsImage()
    {
        // Foto retrato 4000x6000 (aspect 1.5), quadro 1.91:1 (ratio 0.5236).
        const double aspect = 1.5, ratio = 0.5236;
        // A 0° a trava era redundante: um dos termos já vale exatamente 1.0.
        QVERIFY(qFuzzyCompare(cropMaxScale(0.0, aspect, ratio), 1.0));
        // A -90° o 1,91:1 vira 1:1,91 e enche a altura: scale máximo ~1.5.
        QVERIFY2(cropMaxScale(-90.0, aspect, ratio) > 1.4,
                 qPrintable(QStringLiteral("got %1").arg(cropMaxScale(-90.0, aspect, ratio))));
        // normalizeCrop deixa o quadro chegar a esse tamanho (antes travava em 1).
        const Crop norm =
            normalizeCrop(Crop{10.0, 0.5, 0.5, -90.0, ratio}, aspect, instagramRatioValues());
        QVERIFY2(norm.scale > 1.4, qPrintable(QStringLiteral("got %1").arg(norm.scale)));
        // E o recorte resultante enche a altura da foto (~6000 px, sem estourar).
        cv::Mat img(6000, 4000, CV_8UC3, cv::Scalar(10, 20, 30));
        const cv::Mat out = applyCrop(img, norm);
        const int longSide = std::max(out.rows, out.cols);
        QVERIFY2(longSide >= 5990 && longSide <= 6000, qPrintable(QStringLiteral("got %1").arg(longSide)));
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

    void levelsMatchHandLut()
    {
        const cv::Mat img = ramps();
        Look look;
        look.levels[ChannelRgb] = normalizeLevelsChannel({20, 1.6, 230, 10, 240});
        look.levels[ChannelRed] = normalizeLevelsChannel({5, 0.8, 250, 0, 255});
        look.levels[ChannelBlue] = normalizeLevelsChannel({0, 1.0, 255, 255, 0});  // inverte
        const cv::Mat out = render(img, kNeutral, {}, look);
        int worst = 0;
        for (int r = 0; r < img.rows; ++r) {
            for (int c = 0; c < img.cols; ++c) {
                const cv::Vec3b in = img.at<cv::Vec3b>(r, c), got = out.at<cv::Vec3b>(r, c);
                for (int k = 0; k < 3; ++k) {
                    double v = handLevels(in[k], 20, 1.6, 230, 10, 240);
                    if (k == 0)
                        v = handLevels(v, 5, 0.8, 250, 0, 255);
                    if (k == 2)
                        v = handLevels(v, 0, 1.0, 255, 255, 0);
                    const int want = static_cast<int>(v + 0.5);
                    worst = std::max(worst, std::abs(want - got[k]));
                }
            }
        }
        QVERIFY2(worst <= 1, qPrintable(QStringLiteral("max diff %1").arg(worst)));
    }

    void levelsNormalize()
    {
        const LevelsChannel l = normalizeLevelsChannel({254, 20.0, 100, -4, 300});
        QCOMPARE(l.inBlack, 253);
        QCOMPARE(l.inWhite, 255);
        QCOMPARE(l.gamma, 9.99);
        QCOMPARE(l.outBlack, 0);
        QCOMPARE(l.outWhite, 255);
        QCOMPARE(normalizeLevelsChannel({100, 0.01, 50, 0, 255}).inWhite, 102);
        QCOMPARE(normalizeLevelsChannel({100, 0.01, 50, 0, 255}).gamma, 0.1);
        // JSON: só os canais fora da identidade.
        Levels lv{};
        lv[ChannelGreen] = normalizeLevelsChannel({10, 1.25, 240, 0, 255});
        const QJsonObject json = levelsToJson(lv);
        QCOMPARE(json.keys(), QStringList{QStringLiteral("g")});
        QVERIFY(levelsFromJson(json) == lv);
    }

    void curvesInterpolation()
    {
        const Curve c = normalizeCurve({{1.0, 1.0}, {0.25, 0.15}, {0.0, 0.05}, {0.7, 0.85}, {0.25, 0.2}});
        // Ordenada, x único (fica o último), extremos que se movem.
        QCOMPARE(c.size(), size_t(4));
        QCOMPARE(c[1].y, 0.2);
        for (const CurvePoint &p : c)
            QVERIFY(std::abs(evalCurve(c, p.x) - p.y) < 1e-12);
        QCOMPARE(evalCurve(c, 0.0), 0.05);
        // Pontos monótonos -> curva monótona, sem passar dos vizinhos.
        double prev = -1.0;
        for (int i = 0; i <= 1000; ++i) {
            const double y = evalCurve(c, i / 1000.0);
            QVERIFY(y >= prev - 1e-12);
            QVERIFY(y >= 0.0 && y <= 1.0);
            prev = y;
        }
        // Identidade e casos degenerados.
        QVERIFY(normalizeCurve({{0, 0}, {1, 1}}).empty());
        QVERIFY(normalizeCurve({{0.5, 0.5}}).empty());
        Curve many;
        for (int i = 0; i < 30; ++i)
            many.push_back({i / 29.0, i / 29.0});
        const Curve capped = normalizeCurve(many);
        QCOMPARE(static_cast<int>(capped.size()), kCurveMaxPoints);
        QCOMPARE(capped.back().x, 1.0);
        // Dois pontos = reta entre eles.
        const Curve line = normalizeCurve({{0.0, 0.1}, {1.0, 0.9}});
        QVERIFY(std::abs(evalCurve(line, 0.5) - 0.5) < 1e-12);
    }

    void curvesRender()
    {
        const cv::Mat img = ramps();
        Look look;
        look.curves[ChannelRgb] = normalizeCurve({{0.0, 0.0}, {0.25, 0.18}, {0.75, 0.85}, {1.0, 1.0}});
        look.curves[ChannelGreen] = normalizeCurve({{0.0, 0.1}, {1.0, 0.9}});
        const cv::Mat out = render(img, kNeutral, {}, look);
        int worst = 0;
        for (int c = 0; c < img.cols; ++c) {
            const cv::Vec3b in = img.at<cv::Vec3b>(1, c), got = out.at<cv::Vec3b>(1, c);
            for (int k = 0; k < 3; ++k) {
                double v = evalCurve(look.curves[ChannelRgb], in[k] / 255.0);
                if (k == 1)
                    v = evalCurve(look.curves[ChannelGreen], v);
                worst = std::max(worst, std::abs(static_cast<int>(v * 255.0 + 0.5) - got[k]));
            }
        }
        QVERIFY2(worst <= 1, qPrintable(QStringLiteral("max diff %1").arg(worst)));
        // Curva identidade = nada muda.
        Look identity;
        identity.curves[ChannelRed] = normalizeCurve({{0, 0}, {1, 1}});
        QVERIFY(identity.isIdentity());
        QVERIFY(sameImage(render(img, kNeutral, {}, identity), img));
    }

    void effectsAtZeroAreIdentity_data()
    {
        QTest::addColumn<QString>("id");
        for (const auto &e : effectCatalog())
            QTest::newRow(e.id) << QString::fromLatin1(e.id);
    }

    void effectsAtZeroAreIdentity()
    {
        QFETCH(QString, id);
        const cv::Mat img = loadRgb(QStringLiteral("input.png"));
        Look look;
        look.effects = normalizeEffects({{id, 0}});
        QVERIFY(look.effects.empty());
        QVERIFY(sameImage(render(img, kNeutral, {}, look, 42), img));
        // Mesmo sem normalizar: amount 0 não toca o pixel.
        cv::Mat x;
        img.convertTo(x, CV_32FC3, 1.0 / 255.0);
        Look raw;
        raw.effects = {{id, 0}};
        applyLook(x, raw, 42);
        cv::Mat back;
        x.convertTo(back, CV_8UC3, 255.0);
        QVERIFY(sameImage(back, img));
        // E com amount > 0 o efeito aparece.
        look.effects = normalizeEffects({{id, 60}});
        QVERIFY(!sameImage(render(img, kNeutral, {}, look, 42), img));
    }

    void effectsCatalogOrder()
    {
        const Effects e = normalizeEffects({{QStringLiteral("grain"), 20}, {QStringLiteral("nope"), 50},
                                            {QStringLiteral("bw"), 150}, {QStringLiteral("grain"), 30}});
        QCOMPARE(e.size(), size_t(2));
        QCOMPARE(e[0].id, QStringLiteral("bw"));
        QCOMPARE(e[0].amount, 100);
        QCOMPARE(e[1].amount, 30);
        QVERIFY(effectsFromJson(effectsToJson(e)) == e);
    }

    void grainIsDeterministic()
    {
        const cv::Mat img = loadRgb(QStringLiteral("input.png"));
        Look look;
        look.effects = {{QStringLiteral("grain"), 80}};
        const quint64 seed = grainSeed(QStringLiteral("0123456789abcdef0123456789abcdef"));
        QCOMPARE(seed, grainSeed(QStringLiteral("0123456789abcdef0123456789abcdef")));
        const cv::Mat a = render(img, kNeutral, {}, look, seed);
        const cv::Mat b = render(img, kNeutral, {}, look, seed);
        QVERIFY(sameImage(a, b));
        QVERIFY(!sameImage(a, render(img, kNeutral, {}, look, seed + 1)));
        QVERIFY(!sameImage(a, img));
    }

    void vignetteIsSymmetric()
    {
        const cv::Mat img(61, 101, CV_8UC3, cv::Scalar(180, 160, 140));
        Look look;
        look.effects = {{QStringLiteral("vignette"), 100}};
        const cv::Mat out = render(img, kNeutral, {}, look);
        cv::Mat flipH, flipV;
        cv::flip(out, flipH, 1);
        cv::flip(out, flipV, 0);
        QVERIFY(sameImage(out, flipH));
        QVERIFY(sameImage(out, flipV));
        // Centro intacto, cantos escuros.
        QVERIFY(sameImage(out(cv::Rect(50, 30, 1, 1)), img(cv::Rect(50, 30, 1, 1))));
        QVERIFY(out.at<cv::Vec3b>(0, 0)[0] < 100);
    }

    void identityLookKeepsPythonOutput()
    {
        // Look identidade (e semente qualquer) = a mesma saída de antes, byte a byte.
        const cv::Mat img = loadRgb(QStringLiteral("input.png"));
        const QJsonObject all = cases();
        for (const auto &key : all.keys()) {
            if (key.startsWith(u'_'))
                continue;
            const QJsonObject c = all.value(key).toObject();
            const Values v = valuesFromJson(c.value(QStringLiteral("values")).toObject());
            const QString preset = c.value(QStringLiteral("preset")).toString();
            QVERIFY2(sameImage(render(img, v, preset), render(img, v, preset, Look{}, 1234)), qPrintable(key));
        }
        QVERIFY(sameImage(render(img, kNeutral, {}, Look{}, 99), img));
    }

    void lookHash()
    {
        Values v = kNeutral;
        v[Contrast] = 12;
        const QString base = settingsHash(v, {});
        QCOMPARE(settingsHash(v, {}, kCropIdentity, {}, false, Look{}), base);
        Look look;
        look.levels[ChannelRgb] = normalizeLevelsChannel({10, 1.0, 255, 0, 255});
        const QString withLevels = settingsHash(v, {}, kCropIdentity, {}, false, look);
        QVERIFY(withLevels != base);
        look.curves[ChannelBlue] = normalizeCurve({{0, 0}, {0.5, 0.6}, {1, 1}});
        QVERIFY(settingsHash(v, {}, kCropIdentity, {}, false, look) != withLevels);
        // Camada: look identidade não muda o hash; fora dela, muda.
        Layer layer;
        layer.id = QStringLiteral("abc");
        layer.mask = QStringLiteral("0123456789abcdef0123");
        const QString layered = settingsHash(v, {}, kCropIdentity, {layer});
        layer.look.effects = {{QStringLiteral("bw"), 100}};
        QVERIFY(settingsHash(v, {}, kCropIdentity, {layer}) != layered);
        // O JSON das camadas só ganha as chaves quando há algo nelas.
        Layer plain;
        plain.id = QStringLiteral("x1");
        plain.mask = layer.mask;
        QVERIFY(!layersToJson({plain}).at(0).toObject().contains(QStringLiteral("levels")));
        const auto back = normalizeLayers(layersToJson({layer}));
        QVERIFY(back.front().look == layer.look);
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
