// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>

namespace pass {

// Abre (o crea) la base de datos SQLite, activa foreign_keys y aplica las
// migraciones pendientes. Cada instancia usa una conexión Qt con nombre único,
// lo que permite varias bases en paralelo (p. ej. ":memory:" en tests).
class Database {
public:
    explicit Database(const QString& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool isOpen() const { return m_ok; }
    QSqlDatabase handle() const;

    // %APPDATA%/Pass/pass.db (crea el directorio si no existe)
    static QString defaultPath();

private:
    QString m_connName;
    bool m_ok = false;
};

} // namespace pass
