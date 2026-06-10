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
        QCOMPARE(pass::schemaVersion(db.handle()), 1);
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
        QCOMPARE(pass::schemaVersion(db.handle()), 1);
    }
};

QTEST_GUILESS_MAIN(MigrationsTest)
#include "tst_migrations.moc"
