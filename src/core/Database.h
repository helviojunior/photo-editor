// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Banco SQLite do evento: <projeto>/project_data/db.sqlite3.
//
// O esquema é o MESMO da versão em Python (tabelas photoeditor_*): um projeto
// aberto por ela abre aqui sem conversão. Os passos do esquema moram em
// src/core/schema/NNNN.sql e migram de forma INCREMENTAL pelo
// PRAGMA user_version (regra 13): banco legado sem user_version começa do
// número de migrations Django que ele já aplicou (tabela django_migrations).
//
// Só a thread principal usa o banco: trabalho em segundo plano (exportar,
// renderizar) recebe os dados prontos e devolve o resultado por sinal.

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <initializer_list>
#include <stdexcept>

namespace pe {

class DatabaseError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class Database
{
public:
    Database() = default;
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// Abre (cria e migra, se preciso). ``path`` vazio = banco em memória.
    void open(const QString &path);
    void close();
    bool isOpen() const;

    QSqlDatabase handle() const;
    int schemaVersion() const;

    /// Prepara, liga os parâmetros posicionais e executa; lança DatabaseError.
    QSqlQuery exec(const QString &sql, std::initializer_list<QVariant> params = {}) const;
    /// Primeira coluna da primeira linha (ou inválido).
    QVariant scalar(const QString &sql, std::initializer_list<QVariant> params = {}) const;

    void transaction();
    void commit();
    void rollback();

    /// Número de passos de esquema que o app conhece.
    static int latestSchemaVersion();

    /// Id novo no formato do Django (UUID hex de 32 caracteres).
    static QString newId();
    /// Instante no formato que o Django grava no SQLite (UTC, sem fuso).
    static QString timestamp(const QDateTime &when = QDateTime::currentDateTimeUtc());
    static QDateTime parseTimestamp(const QVariant &value);

private:
    void migrate();
    int legacyVersion() const;

    QString m_connection;
};

/// Transação RAII: desfaz se sair do escopo sem commit().
class Transaction
{
public:
    explicit Transaction(Database &db);
    ~Transaction();
    void commit();

private:
    Database &m_db;
    bool m_done = false;
};

} // namespace pe
