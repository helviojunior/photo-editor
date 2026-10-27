// SPDX-License-Identifier: BSD-2-Clause
#include "core/Database.h"

#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSqlError>
#include <QTimeZone>
#include <QUuid>

#include <atomic>

Q_LOGGING_CATEGORY(lcDb, "photoeditor.db")

namespace pe {

namespace {

// Os nomes das migrations Django, na ordem: o passo N do esquema C++ é a
// migration N. Banco legado aplicou um prefixo desta lista.
const char *const kDjangoMigrations[] = {
    "0001_initial",         "0002_history",           "0003_adjustment",     "0004_photo_exported_hash",
    "0005_crop",            "0006_layers",            "0007_merge",          "0008_remove_merge_output",
    "0009_merge_base_area", "0010_photo_copy_of",     "0011_photo_is_cover", "0012_instagram",
};

// Passos do esquema (src/core/schema). Acrescente o próximo; nunca edite um
// já publicado.
constexpr int kSchemaSteps = 15;

QStringList splitStatements(const QString &script)
{
    QStringList out;
    for (const QString &line : script.split(u'\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("--")))
            continue;
        out << trimmed;
    }
    // Cada comando gerado pelo sqlmigrate ocupa uma linha terminada em ';'.
    return out;
}

} // namespace

Database::~Database()
{
    close();
}

void Database::open(const QString &path)
{
    close();
    static std::atomic<int> counter{0};
    m_connection = QStringLiteral("photoeditor-%1").arg(counter++);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path.isEmpty() ? QStringLiteral(":memory:") : path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
    if (!db.open())
        throw DatabaseError(db.lastError().text().toStdString());
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    if (!path.isEmpty())
        exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    migrate();
}

void Database::close()
{
    if (m_connection.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
    m_connection.clear();
}

bool Database::isOpen() const
{
    return !m_connection.isEmpty() && handle().isOpen();
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connection, false);
}

int Database::schemaVersion() const
{
    return scalar(QStringLiteral("PRAGMA user_version")).toInt();
}

int Database::latestSchemaVersion()
{
    return kSchemaSteps;
}

QSqlQuery Database::exec(const QString &sql, std::initializer_list<QVariant> params) const
{
    QSqlQuery q(handle());
    const auto fail = [&]() {
        const QString message = q.lastError().text() + QStringLiteral(" | ") + sql;
        qCWarning(lcDb).noquote() << message;
        throw DatabaseError(message.toStdString());
    };
    if (!q.prepare(sql))
        fail();
    for (const QVariant &p : params) {
        // QString nula viraria NULL e as colunas de texto do Django são NOT
        // NULL: texto é sempre texto; NULL de verdade é um QVariant() vazio.
        if (p.typeId() == QMetaType::QString && p.toString().isNull())
            q.addBindValue(QStringLiteral(""));
        else
            q.addBindValue(p);
    }
    if (!q.exec())
        fail();
    return q;
}

QVariant Database::scalar(const QString &sql, std::initializer_list<QVariant> params) const
{
    QSqlQuery q = exec(sql, params);
    return q.next() ? q.value(0) : QVariant();
}

void Database::transaction()
{
    if (!handle().transaction())
        throw DatabaseError(handle().lastError().text().toStdString());
}

void Database::commit()
{
    if (!handle().commit())
        throw DatabaseError(handle().lastError().text().toStdString());
}

void Database::rollback()
{
    handle().rollback();
}

QString Database::newId()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

QString Database::timestamp(const QDateTime &when)
{
    return when.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz000"));
}

QDateTime Database::parseTimestamp(const QVariant &value)
{
    if (value.isNull())
        return {};
    QString text = value.toString().trimmed();
    if (text.isEmpty())
        return {};
    // "2026-10-15 21:24:30.123456" (Django) ou ISO com 'T'/fuso.
    text.replace(u'T', u' ');
    QString frac;
    const int dot = text.indexOf(u'.');
    QString base = text;
    QString tz;
    if (const int plus = text.indexOf(QRegularExpression(QStringLiteral("[+-]\\d\\d:?\\d\\d$"))); plus > 10) {
        tz = text.mid(plus);
        base = text.left(plus);
    }
    if (dot > 0) {
        frac = base.mid(dot + 1);
        base = base.left(dot);
    }
    QDateTime dt = QDateTime::fromString(base, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!dt.isValid())
        return {};
    dt.setTimeZone(QTimeZone::UTC);
    if (!frac.isEmpty())
        dt = dt.addMSecs(frac.left(3).leftJustified(3, u'0').toInt());
    if (!tz.isEmpty()) {
        const int sign = tz.startsWith(u'-') ? -1 : 1;
        const QString digits = tz.mid(1).remove(u':');
        const int offset = sign * (digits.left(2).toInt() * 3600 + digits.mid(2, 2).toInt() * 60);
        dt = dt.addSecs(-offset);
    }
    return dt;
}

int Database::legacyVersion() const
{
    // Banco criado pela versão em Python: quantas migrations Django ele aplicou.
    const bool hasDjango = scalar(QStringLiteral(
        "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'django_migrations'")).toInt() > 0;
    if (!hasDjango)
        return 0;
    QSqlQuery q = exec(QStringLiteral("SELECT name FROM django_migrations WHERE app = 'photoeditor'"));
    QStringList applied;
    while (q.next())
        applied << q.value(0).toString();
    int version = 0;
    for (const char *name : kDjangoMigrations) {
        if (!applied.contains(QLatin1String(name)))
            break;
        ++version;
    }
    return version;
}

void Database::migrate()
{
    int version = schemaVersion();
    if (version == 0) {
        version = legacyVersion();
        if (version > 0) {
            qCInfo(lcDb) << "Legacy catalog at Django migration" << version;
            exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
        }
    }
    if (version > kSchemaSteps) {
        throw DatabaseError("This project was created by a newer version of PhotoEditor.");
    }
    for (int step = version + 1; step <= kSchemaSteps; ++step) {
        QFile file(QStringLiteral(":/photoeditor/schema/%1.sql").arg(step, 4, 10, QLatin1Char('0')));
        if (!file.open(QIODevice::ReadOnly))
            throw DatabaseError(("Missing schema step " + std::to_string(step)));
        const QStringList statements = splitStatements(QString::fromUtf8(file.readAll()));
        // As reconstruções de tabela do Django exigem as FKs desligadas.
        exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
        transaction();
        try {
            for (const QString &sql : statements)
                exec(sql);
            exec(QStringLiteral("PRAGMA user_version = %1").arg(step));
            commit();
        } catch (...) {
            rollback();
            exec(QStringLiteral("PRAGMA foreign_keys = ON"));
            throw;
        }
        exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        qCInfo(lcDb) << "Catalog schema migrated to" << step;
    }
}

Transaction::Transaction(Database &db)
    : m_db(db)
{
    m_db.transaction();
}

Transaction::~Transaction()
{
    if (!m_done)
        m_db.rollback();
}

void Transaction::commit()
{
    m_db.commit();
    m_done = true;
}

} // namespace pe
