// SPDX-License-Identifier: BSD-2-Clause
#pragma once

#include <QString>

namespace pe {

/// ``os.stat(path).st_mtime_ns`` do Python: nanossegundos desde 1970 (UTC).
/// O QFileInfo só tem milissegundos, e o mtime em ns entra no exported_hash e
/// nas chaves de cache — com outro valor, o primeiro rescan depois da versão
/// em Python "mudaria" todas as fotos e forçaria reexportar tudo. 0 = erro.
qint64 fileMtimeNs(const QString &path);

} // namespace pe
