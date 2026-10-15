// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Identidade do resultado: mesmos ajustes + mesma versão do motor = mesma
// imagem. O hash é o MESMO que a versão em Python gravava
// (sha1(json.dumps(..., sort_keys=True))[:16]) — por isso o JSON é montado à
// mão, imitando o json do Python (separadores ", " e ": ", repr dos floats).
// Assim um projeto aberto na versão C++ não reexporta tudo à toa.

#include "imaging/Crop.h"
#include "imaging/Develop.h"

#include <QString>

#include <vector>

namespace pe::imaging {

/// repr() de um float do Python: o menor texto que volta ao mesmo double
/// ("0.5", "1.0", "1e-05", "15.0").
QString pyFloat(double value);

/// ``pristine``: a foto nunca foi editada (sem linha em photoeditor_adjustment).
/// O Python montava então os valores com ``exposure`` int 0 em vez de 0.0, o
/// que muda o JSON e o hash; depois da primeira edição é sempre float.
QString settingsHash(const Values &values, const QString &preset, const Crop &crop = kCropIdentity,
                     const std::vector<Layer> &layers = {}, bool pristine = false);

/// sha1 hex de um texto UTF-8.
QString sha1Hex(const QByteArray &data);

} // namespace pe::imaging
