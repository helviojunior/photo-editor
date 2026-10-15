// SPDX-License-Identifier: BSD-2-Clause
#include "core/FileTime.h"

#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace pe {

qint64 fileMtimeNs(const QString &path)
{
#ifdef Q_OS_WIN
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(reinterpret_cast<const wchar_t *>(path.utf16()), GetFileExInfoStandard, &data))
        return 0;
    // FILETIME: intervalos de 100 ns desde 1601 — a mesma conta do CPython.
    const qint64 ticks = (static_cast<qint64>(data.ftLastWriteTime.dwHighDateTime) << 32)
        | data.ftLastWriteTime.dwLowDateTime;
    return (ticks - 116444736000000000LL) * 100;
#else
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return 0;
#if defined(Q_OS_DARWIN)
    return static_cast<qint64>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    return static_cast<qint64>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
#endif
}

} // namespace pe
