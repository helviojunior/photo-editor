// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// EXIF de JPEG sem biblioteca externa: um leitor/escritor TIFF mínimo, só com
// o que o editor usa.
//
// Leitura (``io.py``): Orientation e a hora de captura (DateTimeOriginal +
// SubsecTimeOriginal + OffsetTimeOriginal).
//
// Escrita (``publish.py: _exif_for_publication``): o EXIF da foto publicada é
// o ORIGINAL com Orientation = 1, sem a miniatura (IFD1) e com ``Software`` =
// este editor. Em vez de remontar o TIFF inteiro (como o piexif fazia), o blob
// original fica intacto — os offsets absolutos de MakerNote e sub-IFDs
// continuam valendo — e um IFD0 NOVO é acrescentado no fim, com o cabeçalho
// apontando para ele. A miniatura, se estiver na cauda, é cortada.
//
// "EXIF" aqui é o conteúdo do segmento APP1: ``"Exif\0\0"`` + TIFF — o mesmo
// que o ``img.info['exif']`` do Pillow. As funções aceitam também o TIFF puro.

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace pe::imaging {

inline constexpr int kExifOrientation = 0x0112;
inline constexpr int kExifSoftware = 0x0131;
inline constexpr int kExifIfdPointer = 0x8769;
inline constexpr int kExifDateTimeOriginal = 0x9003;
inline constexpr int kExifOffsetTimeOriginal = 0x9011;
inline constexpr int kExifSubsecTimeOriginal = 0x9291;

/// Maior conteúdo de um segmento APP1 (o campo de tamanho, 16 bits, conta a si mesmo).
inline constexpr int kMaxApp1Payload = 65535 - 2;

struct ExifInfo
{
    bool valid = false;     ///< o TIFF foi lido (cabeçalho + IFD0)
    int orientation = 1;    ///< 1..8; fora disso (ou ausente) = 1
    QDateTime capturedAt;   ///< UTC; inválido se não há/não dá para ler
    QString dateTimeOriginal;
    QString subsecTimeOriginal;
    QString offsetTimeOriginal;
    QString software;       ///< tag Software do IFD0 (sem o NUL)
    bool hasIfd1 = false;   ///< há IFD1 (a miniatura) encadeado ao IFD0
};

/// Lê o EXIF (``"Exif\0\0"`` + TIFF, ou TIFF puro). Nunca lança.
ExifInfo parseExif(const QByteArray &exif);

/// Hora de captura a partir dos três textos do EXIF (``_captured_at``). Sem
/// offset gravado, a câmera marca a hora local do evento: interpreta no fuso
/// LOCAL do sistema. Devolve em UTC; inválido se a data não for lida.
QDateTime capturedAtFromExif(const QString &dateTimeOriginal, const QString &subsec,
                             const QString &offset);

/// EXIF para a foto publicada: Orientation = 1, sem IFD1/miniatura e, se
/// ``software`` não for vazio, ``Software`` = ``software`` (ASCII; o que não
/// for ASCII vira '?'). EXIF vazio ou ilegível vira um só com Orientation e
/// Software. Se o resultado não couber num APP1, sai um mínimo com Orientation,
/// Software e as três tags de data. Devolve ``"Exif\0\0"`` + TIFF.
QByteArray exifForPublication(const QByteArray &exif, const QString &software);

/// Insere ``exif`` (``"Exif\0\0"`` + TIFF) como APP1 logo depois do APP0 (ou
/// do SOI, se não houver APP0) e remove APP1 Exif que já existirem. Vazio se
/// ``jpeg`` não for JPEG ou o EXIF não couber num segmento.
QByteArray insertExif(const QByteArray &jpeg, const QByteArray &exif);

} // namespace pe::imaging
