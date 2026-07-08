// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Database.h"
#include "pass/db/Migrations.h"

#include <QSqlQuery>
#include <QtTest>

class MigrationsTest : public QObject {
    Q_OBJECT

private slots:
    void opensAndMigratesInMemory() {
        pass::Database db(QStringLiteral(":memory:"));
        QVERIFY(db.isOpen());
        QCOMPARE(pass::schemaVersion(db.handle()), 5);
    }

    void createsAllTables() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")));
        QStringList tables;
        while (q.next())
            tables << q.value(0).toString();
        QVERIFY(tables.contains(QStringLiteral("subjects")));
        QVERIFY(tables.contains(QStringLiteral("strategies")));
        QVERIFY(tables.contains(QStringLiteral("sessions")));
        QVERIFY(tables.contains(QStringLiteral("events")));
        QVERIFY(tables.contains(QStringLiteral("sync_state")));
        QVERIFY(tables.contains(QStringLiteral("topics")));
    }

    void v2AddsEtagAndSyncState() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        // events.etag existe.
        QVERIFY(q.exec(QStringLiteral("SELECT etag FROM events")));
        // El índice único parcial permite dos filas locales sin external_id...
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO events(id, title, start_utc, end_utc) VALUES('a','t','x','y')")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO events(id, title, start_utc, end_utc) VALUES('b','t','x','y')")));
        // ...pero impide duplicar (provider, external_id) cuando hay external_id.
        QVERIFY(q.exec(QStringLiteral("INSERT INTO events(id, provider, external_id, title, "
                                      "start_utc, end_utc) VALUES('c','google','EXT','t','x','y')")));
        QVERIFY(!q.exec(QStringLiteral("INSERT INTO events(id, provider, external_id, title, "
                                       "start_utc, end_utc) VALUES('d','google','EXT','t','x','y')")));
    }

    void foreignKeysEnabled() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    void migrationsAreIdempotent() {
        pass::Database db(QStringLiteral(":memory:"));
        QVERIFY(db.isOpen());
        QVERIFY(pass::applyMigrations(db.handle()));
        QCOMPARE(pass::schemaVersion(db.handle()), 5);
    }

    // v4: la tabla topics existe y aplica UNIQUE(subject_id, name).
    void v4AddsTopics() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO subjects(id, name) VALUES('s1','Mates')")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO topics(id, subject_id, name, updated_at) "
                                      "VALUES('t1','s1','Integrales','2026-01-01T00:00:00Z')")));
        // Mismo nombre en la misma asignatura: rechazado por UNIQUE.
        QVERIFY(!q.exec(QStringLiteral("INSERT INTO topics(id, subject_id, name, updated_at) "
                                       "VALUES('t2','s1','Integrales','2026-01-01T00:00:00Z')")));
        // Mismo nombre en otra asignatura: permitido.
        QVERIFY(q.exec(QStringLiteral("INSERT INTO subjects(id, name) VALUES('s2','Física')")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO topics(id, subject_id, name, updated_at) "
                                      "VALUES('t3','s2','Integrales','2026-01-01T00:00:00Z')")));
    }

    // v5: columnas de progreso para retomar sesiones interrumpidas.
    void v5AddsResumeColumns() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        // Existen y aceptan valores; NULL = no retomable.
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO sessions(id, status, resume_phase_index, resume_phase_elapsed_sec) "
            "VALUES('s1','aborted',2,90)")));
        QVERIFY(q.exec(QStringLiteral(
            "SELECT resume_phase_index, resume_phase_elapsed_sec FROM sessions WHERE id='s1'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QCOMPARE(q.value(1).toInt(), 90);
    }

    void v3AddsUpdatedAtAndTombstones() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        // updated_at existe en las tres tablas y quedó backfilleado (no nulo).
        for (const QString& table :
             {QStringLiteral("subjects"), QStringLiteral("strategies"), QStringLiteral("sessions")}) {
            QVERIFY2(q.exec(QStringLiteral("SELECT updated_at FROM %1").arg(table)),
                     qPrintable(table));
        }
        // Las estrategias builtin (sembradas) tienen updated_at no nulo.
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM strategies WHERE updated_at IS NULL")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        // tombstones existe con la clave compuesta esperada.
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO tombstones(entity, id, deleted_at) VALUES('subjects','x','t')")));
        QVERIFY(!q.exec(QStringLiteral(
            "INSERT INTO tombstones(entity, id, deleted_at) VALUES('subjects','x','t2')")));
    }

    void builtinStrategiesHaveDeterministicIds() {
        pass::Database db(QStringLiteral(":memory:"));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral("SELECT id, name FROM strategies WHERE builtin = 1")));
        int seen = 0;
        while (q.next()) {
            ++seen;
            const QString id = q.value(0).toString();
            const QString name = q.value(1).toString();
            QCOMPARE(id, pass::builtinStrategyId(name).toString(QUuid::WithoutBraces));
        }
        QCOMPARE(seen, 3);
    }

    // Una BD heredada en v2 con ids builtin aleatorios debe reasignarlos al UUIDv5
    // determinista, arrastrando en cascada las sesiones que los referencian.
    void v3RemapsBuiltinIdsInCascade() {
        const QString conn = QStringLiteral("tst_v2_legacy");
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            raw.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            // Esquema mínimo pre-v3 (sin updated_at) suficiente para migrateV3.
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE subjects(id TEXT PRIMARY KEY, name TEXT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE strategies(id TEXT PRIMARY KEY, name TEXT, builtin INTEGER)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE sessions(id TEXT PRIMARY KEY, strategy_id TEXT, "
                "started_at TEXT, ended_at TEXT)")));

            const QString randomId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QString name = QStringLiteral("Pomodoro 25/5");
            q.prepare(QStringLiteral(
                "INSERT INTO strategies(id, name, builtin) VALUES(?, ?, 1)"));
            q.addBindValue(randomId);
            q.addBindValue(name);
            QVERIFY(q.exec());
            q.prepare(QStringLiteral(
                "INSERT INTO sessions(id, strategy_id, started_at) VALUES(?, ?, ?)"));
            q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
            q.addBindValue(randomId);
            q.addBindValue(QStringLiteral("2026-01-01T10:00:00Z"));
            QVERIFY(q.exec());

            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 2")));
            QVERIFY(pass::applyMigrations(raw));
            QCOMPARE(pass::schemaVersion(raw), 5);

            const QString expected = pass::builtinStrategyId(name).toString(QUuid::WithoutBraces);
            QVERIFY(q.exec(QStringLiteral("SELECT id FROM strategies WHERE builtin = 1")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), expected);
            QVERIFY(q.exec(QStringLiteral("SELECT strategy_id FROM sessions")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), expected);
            // El backfill de sesiones usa started_at cuando no hay ended_at.
            QVERIFY(q.exec(QStringLiteral("SELECT updated_at FROM sessions")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("2026-01-01T10:00:00Z"));
        }
        QSqlDatabase::removeDatabase(conn);
    }
};

QTEST_GUILESS_MAIN(MigrationsTest)
#include "tst_migrations.moc"
