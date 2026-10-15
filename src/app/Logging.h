// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Log em ~/.photoe/logs/app.log (rotativo, 5 MB x 3) e, quando há terminal,
// também no stderr. O app instalado não tem terminal: log que não vai para
// arquivo não existe. Nos módulos: Q_LOGGING_CATEGORY + qCInfo/qCWarning.
//
// Nível: --debug liga o debug de tudo; PHOTOEDITOR_LOG_LEVEL (debug | info |
// warning) também, sem precisar de argumento.

#include <QString>

namespace pe::app {

void setupLogging(bool debug);
QString logFile();

} // namespace pe::app
