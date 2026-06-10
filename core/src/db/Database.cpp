// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Database.h"

#include "pass/db/Migrations.h"

#include <QAtomicInt>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>

namespace pass {

namespace {
QAtomicInt connectionCounter;
}

Database::Database(const QString& path) {
    m_connName = QStringLiteral("pass_conn_%1").arg(connectionCounter.fetchAndAddRelaxed(1));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(path);
    if (!db.open())
        return;

    // SQLite trae las foreign keys desactivadas por defecto en cada conexión.
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")))
        return;

    m_ok = applyMigrations(db);
}

Database::~Database() {
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName, /*open=*/false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connName);
}

QSqlDatabase Database::handle() const {
    return QSqlDatabase::database(m_connName);
}

QString Database::defaultPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/pass.db");
}

} // namespace pass
