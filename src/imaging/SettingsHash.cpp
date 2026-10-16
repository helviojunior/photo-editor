// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/SettingsHash.h"

#include <QCryptographicHash>

#include <charconv>
#include <cmath>
#include <map>
#include <string>

namespace pe::imaging {

namespace {

/// Um valor do slider como o Python o guardaria: int nos de passo inteiro.
QString pyNumber(Slider s, double value, bool intZero)
{
    if (isIntegerSlider(s) || (intZero && value == 0.0))
        return QString::number(static_cast<qint64>(value));
    return pyFloat(value);
}

/// Dicionário de valores efetivos, com as chaves em ordem (sort_keys).
QString valuesDict(const Values &v, bool pristine = false)
{
    std::map<std::string, QString> items;
    for (int i = 0; i < SliderCount; ++i)
        items[sliders()[i].name] = pyNumber(static_cast<Slider>(i), v[i], pristine);
    QString out = QStringLiteral("{");
    bool first = true;
    for (const auto &[key, value] : items) {
        if (!first)
            out += QStringLiteral(", ");
        first = false;
        out += QLatin1Char('"') + QString::fromStdString(key) + QStringLiteral("\": ") + value;
    }
    return out + QLatin1Char('}');
}

} // namespace

QString pyFloat(double value)
{
    if (std::isnan(value))
        return QStringLiteral("NaN");
    if (std::isinf(value))
        return value > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    if (value == 0.0)
        return std::signbit(value) ? QStringLiteral("-0.0") : QStringLiteral("0.0");

    // Dígitos mínimos (round-trip) em notação científica: "d.ddde±XX".
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, value, std::chars_format::scientific);
    std::string sci(buf, res.ptr);
    const auto ePos = sci.find('e');
    std::string mantissa = sci.substr(0, ePos);
    const int exponent = std::stoi(sci.substr(ePos + 1));
    const bool negative = mantissa[0] == '-';
    if (negative)
        mantissa.erase(0, 1);
    std::string digits;
    for (char c : mantissa) {
        if (c != '.')
            digits += c;
    }

    std::string out;
    // float_repr_style 'short' do CPython: notação fixa para 1e-4 <= |x| < 1e16.
    if (exponent >= -4 && exponent < 16) {
        if (exponent >= 0) {
            const int intLen = exponent + 1;
            if (static_cast<int>(digits.size()) <= intLen) {
                out = digits + std::string(intLen - digits.size(), '0') + ".0";
            } else {
                out = digits.substr(0, intLen) + "." + digits.substr(intLen);
            }
        } else {
            out = "0." + std::string(-exponent - 1, '0') + digits;
        }
    } else {
        out = digits.substr(0, 1);
        if (digits.size() > 1)
            out += "." + digits.substr(1);
        char e[16];
        std::snprintf(e, sizeof e, "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
        out += e;
    }
    if (negative)
        out.insert(0, "-");
    return QString::fromStdString(out);
}

QString sha1Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString settingsHash(const Values &values, const QString &preset, const Crop &crop, const std::vector<Layer> &layers,
                     bool pristine, const Look &look)
{
    QString payload = QStringLiteral("[") + QString::number(kEngineVersion) + QStringLiteral(", ")
        + valuesDict(effective(values, preset), pristine);
    // Sem crop, o hash é o mesmo de antes de o crop existir; ratio 0 fica de
    // fora pelo mesmo motivo. O mesmo vale para as camadas.
    if (!isCropIdentity(crop)) {
        // Chaves em ordem alfabética: angle, cx, cy, ratio, scale.
        payload += QStringLiteral(", {\"angle\": ") + pyFloat(crop.angle) + QStringLiteral(", \"cx\": ")
            + pyFloat(crop.cx) + QStringLiteral(", \"cy\": ") + pyFloat(crop.cy);
        if (crop.ratio != 0.0)
            payload += QStringLiteral(", \"ratio\": ") + pyFloat(crop.ratio);
        payload += QStringLiteral(", \"scale\": ") + pyFloat(crop.scale) + QLatin1Char('}');
    }
    if (!layers.empty()) {
        payload += QStringLiteral(", {\"layers\": [");
        for (size_t i = 0; i < layers.size(); ++i) {
            if (i)
                payload += QStringLiteral(", ");
            payload += QStringLiteral("[\"") + layers[i].mask + QStringLiteral("\", ")
                + valuesDict(effective(layers[i].values, layers[i].preset));
            if (!layers[i].look.isIdentity())
                payload += QStringLiteral(", ") + lookHashJson(layers[i].look);
            payload += QLatin1Char(']');
        }
        payload += QStringLiteral("]}");
    }
    if (!look.isIdentity())
        payload += QStringLiteral(", {\"look\": ") + lookHashJson(look) + QLatin1Char('}');
    payload += QLatin1Char(']');
    return sha1Hex(payload.toUtf8()).left(16);
}

} // namespace pe::imaging
