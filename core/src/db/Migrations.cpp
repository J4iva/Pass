// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Migrations.h"

#include <QList>
#include <QSqlQuery>
#include <QUuid>

#include <functional>
#include <initializer_list>

namespace pass {

namespace {

bool execAll(QSqlDatabase& db, std::initializer_list<const char*> statements) {
    QSqlQuery q(db);
    for (const char* sql : statements) {
        if (!q.exec(QString::fromLatin1(sql)))
            return false;
    }
    return true;
}

bool seedStrategy(QSqlDatabase& db, const QString& name, int work, int brk, int longBrk,
                  int cycles) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO strategies(id, name, work_min, break_min, long_break_min, "
        "cycles_before_long, builtin) VALUES(?, ?, ?, ?, ?, ?, 1)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(name);
    q.addBindValue(work);
    q.addBindValue(brk);
    q.addBindValue(longBrk);
    q.addBindValue(cycles);
    return q.exec();
}

bool migrateV1(QSqlDatabase& db) {
    const bool tablesOk = execAll(
        db, {"CREATE TABLE subjects("
             "  id TEXT PRIMARY KEY,"
             "  name TEXT NOT NULL UNIQUE,"
             "  color TEXT,"
             "  archived INTEGER NOT NULL DEFAULT 0)",

             "CREATE TABLE strategies("
             "  id TEXT PRIMARY KEY,"
             "  name TEXT NOT NULL,"
             "  work_min INTEGER NOT NULL,"
             "  break_min INTEGER NOT NULL,"
             "  long_break_min INTEGER NOT NULL,"
             "  cycles_before_long INTEGER NOT NULL,"
             "  builtin INTEGER NOT NULL DEFAULT 0)",

             "CREATE TABLE sessions("
             "  id TEXT PRIMARY KEY,"
             "  subject_id TEXT REFERENCES subjects(id),"
             "  strategy_id TEXT REFERENCES strategies(id),"
             "  topic TEXT,"
             "  planned_min INTEGER,"
             "  actual_sec INTEGER NOT NULL DEFAULT 0,"
             "  started_at TEXT," // ISO 8601 UTC
             "  ended_at TEXT,"
             "  status TEXT NOT NULL CHECK(status IN ('planned','completed','aborted')),"
             "  event_id TEXT)",

             "CREATE TABLE events("
             "  id TEXT PRIMARY KEY,"
             "  provider TEXT NOT NULL DEFAULT 'local',"
             "  external_id TEXT,"
             "  title TEXT NOT NULL,"
             "  description TEXT,"
             "  start_utc TEXT NOT NULL,"
             "  end_utc TEXT NOT NULL,"
             "  all_day INTEGER NOT NULL DEFAULT 0,"
             "  rrule TEXT,"
             "  subject_id TEXT,"
             "  source_session_id TEXT,"
             "  updated_at TEXT)",

             "CREATE INDEX idx_events_start ON events(start_utc)",
             "CREATE INDEX idx_sessions_subject ON sessions(subject_id)"});
    if (!tablesOk)
        return false;

    return seedStrategy(db, QStringLiteral("Pomodoro 25/5"), 25, 5, 15, 4) &&
           seedStrategy(db, QStringLiteral("Pomodoro 45/5"), 45, 5, 20, 3) &&
           seedStrategy(db, QStringLiteral("Pomodoro 50/10"), 50, 10, 25, 2);
}

} // namespace

int schemaVersion(QSqlDatabase db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return -1;
    return q.value(0).toInt();
}

bool applyMigrations(QSqlDatabase db) {
    const QList<std::function<bool(QSqlDatabase&)>> migrations = {migrateV1};

    const int current = schemaVersion(db);
    if (current < 0)
        return false;

    for (int v = current; v < migrations.size(); ++v) {
        if (!db.transaction())
            return false;
        if (!migrations[v](db)) {
            db.rollback();
            return false;
        }
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("PRAGMA user_version = %1").arg(v + 1))) {
            db.rollback();
            return false;
        }
        if (!db.commit())
            return false;
    }
    return true;
}

} // namespace pass
