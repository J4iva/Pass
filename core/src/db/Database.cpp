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

    // WAL permite que el lector (hilo GUI) y el escritor (hilo de sincronización,
    // que abre SU PROPIA conexión al mismo fichero) coexistan sin bloquearse: el
    // lector ve siempre el último commit y nunca recibe SQLITE_BUSY por el writer.
    // busy_timeout hace que, si ambos escriben, el segundo espere en vez de fallar.
    // En ":memory:" (tests) WAL no aplica y devuelve "memory": no es un error.
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

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
