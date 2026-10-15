// SPDX-License-Identifier: BSD-2-Clause
#pragma once

#include <QMutex>
#include <QString>
#include <QThreadPool>

/// Fila dos traços do pincel: cada traço parte da máscara que o anterior
/// deixou. Um pool de uma thread só garante a ordem; ``generation`` descarta
/// os traços de uma seleção já cancelada.
struct SelectionChain
{
    QMutex lock;
    QString mask;
    int generation = 0;
    QThreadPool pool;

    SelectionChain() { pool.setMaxThreadCount(1); }
};
