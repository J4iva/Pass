// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Migrations.h"

#include <QDateTime>
#include <QList>
#include <QSqlQuery>
#include <QUuid>

#include <functional>
#include <initializer_list>

namespace pass {

namespace {

// Namespace fijo para derivar UUIDv5 deterministas de Pass (RFC 4122).
const QUuid kPassNamespace(QStringLiteral("{1b4e28ba-2fa1-11d2-883f-0016d3cca427}"));

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
    q.addBindValue(builtinStrategyId(name).toString(QUuid::WithoutBraces));
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

// v2: soporte de sincronización con Google Calendar (fase 2).
// - events.etag: ETag del recurso remoto, para envíos write-through con If-Match.
// - sync_state: KV para syncToken/last_sync (misma vida que las filas espejo,
//   commit atómico con los upserts; por eso en la DB y no en QSettings).
// - índice único parcial: garantiza un solo evento por (provider, external_id)
//   entre las filas que tienen external_id, soporte del upsert idempotente.
bool migrateV2(QSqlDatabase& db) {
    return execAll(
        db, {"ALTER TABLE events ADD COLUMN etag TEXT",
             "CREATE TABLE sync_state(key TEXT PRIMARY KEY, value TEXT NOT NULL)",
             "CREATE UNIQUE INDEX idx_events_provider_external ON events(provider, external_id) "
             "  WHERE external_id IS NOT NULL AND external_id <> ''"});
}

// v3: base para la sincronización entre dispositivos vía repo de GitHub (fase 3).
// - updated_at en subjects/strategies/sessions (events ya lo tenía): marca de
//   última escritura, base del last-writer-wins por registro al importar.
// - tombstones(entity, id, deleted_at): borrados explícitos (nunca "archivo
//   ausente = borrado"), para que un borrado se propague sin perder datos.
// - UUIDv5 deterministas para las estrategias builtin (hoy aleatorios por
//   dispositivo): así las sesiones sincronizadas referencian la misma estrategia
//   en cualquier máquina. Se reasignan en cascada con sessions.strategy_id.
bool migrateV3(QSqlDatabase& db) {
    QSqlQuery q(db);

    // Reasignar strategies.id en cascada con sessions.strategy_id exige tocar
    // ambos lados de la FK dentro de la transacción: diferir la comprobación
    // hasta el commit evita estados intermedios inválidos.
    if (!q.exec(QStringLiteral("PRAGMA defer_foreign_keys = ON")))
        return false;

    if (!execAll(db, {"ALTER TABLE subjects ADD COLUMN updated_at TEXT",
                      "ALTER TABLE strategies ADD COLUMN updated_at TEXT",
                      "ALTER TABLE sessions ADD COLUMN updated_at TEXT",
                      "CREATE TABLE tombstones("
                      "  entity TEXT NOT NULL,"
                      "  id TEXT NOT NULL,"
                      "  deleted_at TEXT NOT NULL," // ISO 8601 UTC
                      "  PRIMARY KEY(entity, id))"}))
        return false;

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // Backfill de updated_at. En sesiones, la mejor aproximación a "cuándo
    // cambió" sin marca histórica real es su fin (o inicio, o ahora).
    q.prepare(QStringLiteral("UPDATE subjects SET updated_at = ? WHERE updated_at IS NULL"));
    q.addBindValue(now);
    if (!q.exec())
        return false;
    q.prepare(QStringLiteral("UPDATE strategies SET updated_at = ? WHERE updated_at IS NULL"));
    q.addBindValue(now);
    if (!q.exec())
        return false;
    q.prepare(QStringLiteral("UPDATE sessions SET updated_at = COALESCE(ended_at, started_at, ?) "
                             "WHERE updated_at IS NULL"));
    q.addBindValue(now);
    if (!q.exec())
        return false;

    // Reasignar los ids builtin a su UUIDv5 determinista (idempotente: en una BD
    // recién sembrada por migrateV1 ya coinciden y no se toca nada).
    QSqlQuery sel(db);
    if (!sel.exec(QStringLiteral("SELECT id, name FROM strategies WHERE builtin = 1")))
        return false;
    struct Remap {
        QString oldId;
        QString newId;
    };
    QList<Remap> remaps;
    while (sel.next()) {
        const QString oldId = sel.value(0).toString();
        const QString newId =
            builtinStrategyId(sel.value(1).toString()).toString(QUuid::WithoutBraces);
        if (oldId != newId)
            remaps.append({oldId, newId});
    }
    for (const Remap& r : remaps) {
        q.prepare(QStringLiteral("UPDATE strategies SET id = ? WHERE id = ?"));
        q.addBindValue(r.newId);
        q.addBindValue(r.oldId);
        if (!q.exec())
            return false;
        q.prepare(QStringLiteral("UPDATE sessions SET strategy_id = ? WHERE strategy_id = ?"));
        q.addBindValue(r.newId);
        q.addBindValue(r.oldId);
        if (!q.exec())
            return false;
    }
    return true;
}

// v4: los temas pasan a ser una entidad propia (antes solo texto libre en
// sesiones/notas). Gestionables desde Administración y sincronizables entre
// dispositivos (updated_at + tombstones, como el resto). UNIQUE(subject_id,
// name) evita temas duplicados dentro de una asignatura.
bool migrateV4(QSqlDatabase& db) {
    return execAll(db, {"CREATE TABLE topics("
                        "  id TEXT PRIMARY KEY,"
                        "  subject_id TEXT NOT NULL REFERENCES subjects(id),"
                        "  name TEXT NOT NULL,"
                        "  updated_at TEXT,"
                        "  UNIQUE(subject_id, name))",
                        "CREATE INDEX idx_topics_subject ON topics(subject_id)"});
}

// v5: progreso para retomar sesiones interrumpidas. Al cerrar la app con una
// sesión en marcha se guarda la posición (fase actual + segundos consumidos en
// ella); con eso y strategy_id + planned_min se reconstruyen las fases y se
// reanuda donde se quedó. NULL = sesión no retomable (p. ej. terminada a mano).
bool migrateV5(QSqlDatabase& db) {
    return execAll(db, {"ALTER TABLE sessions ADD COLUMN resume_phase_index INTEGER",
                        "ALTER TABLE sessions ADD COLUMN resume_phase_elapsed_sec INTEGER"});
}

} // namespace

QUuid builtinStrategyId(const QString& name) {
    return QUuid::createUuidV5(kPassNamespace, name);
}

int schemaVersion(QSqlDatabase db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return -1;
    return q.value(0).toInt();
}

bool applyMigrations(QSqlDatabase db) {
    const QList<std::function<bool(QSqlDatabase&)>> migrations = {migrateV1, migrateV2, migrateV3,
                                                                  migrateV4, migrateV5};

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
