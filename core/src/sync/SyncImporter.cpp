// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/SyncImporter.h"

#include "pass/sync/SyncSerializer.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include <optional>

namespace pass::sync {

namespace {

constexpr qint64 kMaxFileBytes = 256 * 1024; // ignora archivos sospechosamente grandes

// Entidades válidas y su tabla (whitelist: la entidad viaja en datos remotos, así
// que nunca se interpola directa en SQL).
const QStringList kEntities = {QStringLiteral("subjects"), QStringLiteral("topics"),
                               QStringLiteral("strategies"), QStringLiteral("sessions"),
                               QStringLiteral("events"), QStringLiteral("tombstones")};

QDateTime isoToUtc(const QString& text) {
    QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

// Bind de un id/fecha opcional: cadena vacía => NULL en la columna.
QVariant orNull(const QString& s) {
    return s.isEmpty() ? QVariant() : QVariant(s);
}

QString remapId(const QHash<QString, QString>& remap, const QString& id) {
    const auto it = remap.constFind(id);
    return it != remap.constEnd() ? it.value() : id;
}

// Una ruta del repo segura para importar: data/<entity>/<stem>.json, sin '..'.
bool parseDataPath(const QString& relPath, QString& entity, QString& stem) {
    if (relPath.contains(QStringLiteral("..")))
        return false;
    const QStringList parts = relPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 3 || parts[0] != QStringLiteral("data") || !kEntities.contains(parts[1]))
        return false;
    if (!parts[2].endsWith(QStringLiteral(".json")))
        return false;
    entity = parts[1];
    stem = parts[2].chopped(5);
    return !stem.isEmpty();
}

std::optional<QJsonObject> readObject(const QString& absPath) {
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxFileBytes)
        return std::nullopt;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    return doc.object();
}

struct ParsedData {
    QList<Subject> subjects;
    QList<Topic> topics;
    QList<PomodoroStrategy> strategies;
    QList<CalendarEvent> events;
    QList<std::pair<StudySession, SessionEventRef>> sessions;
    QList<TombstoneRecord> tombstones;
};

// Lee y valida un archivo del espejo. Los archivos corruptos o con id que no
// casa con el nombre se ignoran (no abortan el import).
void categorize(const QString& entity, const QString& stem, const QString& absPath,
                ParsedData& out) {
    const auto obj = readObject(absPath);
    if (!obj)
        return;
    if (entity == QStringLiteral("subjects")) {
        Subject s;
        if (fromJson(*obj, s) && s.id.toString(QUuid::WithoutBraces) == stem)
            out.subjects.append(s);
    } else if (entity == QStringLiteral("topics")) {
        Topic t;
        if (fromJson(*obj, t) && t.id.toString(QUuid::WithoutBraces) == stem)
            out.topics.append(t);
    } else if (entity == QStringLiteral("strategies")) {
        PomodoroStrategy s;
        if (fromJson(*obj, s) && s.id.toString(QUuid::WithoutBraces) == stem)
            out.strategies.append(s);
    } else if (entity == QStringLiteral("events")) {
        CalendarEvent e;
        if (fromJson(*obj, e) && e.id.toString(QUuid::WithoutBraces) == stem)
            out.events.append(e);
    } else if (entity == QStringLiteral("sessions")) {
        StudySession s;
        SessionEventRef ref;
        if (fromJson(*obj, s, ref) && s.id.toString(QUuid::WithoutBraces) == stem)
            out.sessions.append({s, ref});
    } else if (entity == QStringLiteral("tombstones")) {
        TombstoneRecord t;
        if (fromJson(*obj, t) && t.id.toString(QUuid::WithoutBraces) == stem)
            out.tombstones.append(t);
    }
}

// ¿El formato del manifest es soportado? Manifest ausente => compatible.
bool manifestSupported(const QString& repoDir, QString& errOut) {
    const auto obj = readObject(repoDir + QStringLiteral("/manifest.json"));
    if (!obj)
        return true;
    const int format = obj->value(QStringLiteral("format")).toInt(kManifestFormat);
    if (format > kManifestFormat) {
        errOut = QStringLiteral(
            "El repositorio usa un formato de sincronización más nuevo (%1) que esta "
            "versión de Pass (%2). Actualiza la app.")
                     .arg(format)
                     .arg(kManifestFormat);
        return false;
    }
    return true;
}

// --- helpers de escritura sobre la transacción de import -------------------

void clearTombstone(QSqlQuery& q, const char* entity, const QString& id) {
    q.prepare(QStringLiteral("DELETE FROM tombstones WHERE entity = ? AND id = ?"));
    q.addBindValue(QString::fromLatin1(entity));
    q.addBindValue(id);
    q.exec();
}

bool lwwUpsertSubject(QSqlQuery& q, const Subject& s, bool& changed) {
    const QString id = s.id.toString(QUuid::WithoutBraces);
    q.prepare(QStringLiteral(
        "INSERT INTO subjects(id, name, color, archived, updated_at) VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, color = excluded.color, "
        "archived = excluded.archived, updated_at = excluded.updated_at "
        "WHERE excluded.updated_at > subjects.updated_at"));
    q.addBindValue(id);
    q.addBindValue(s.name);
    q.addBindValue(s.colorHex);
    q.addBindValue(s.archived ? 1 : 0);
    q.addBindValue(s.updatedAt.toUTC().toString(Qt::ISODate));
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0) {
        changed = true;
        clearTombstone(q, "subjects", id); // una fila viva no debe tener lápida
    }
    return true;
}

bool lwwUpsertTopic(QSqlQuery& q, const Topic& t, const QHash<QString, QString>& remap,
                    bool& changed) {
    const QString id = t.id.toString(QUuid::WithoutBraces);
    const QString subjectId = remapId(remap, t.subjectId.toString(QUuid::WithoutBraces));
    q.prepare(QStringLiteral(
        "INSERT INTO topics(id, subject_id, name, updated_at) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET subject_id = excluded.subject_id, name = excluded.name, "
        "updated_at = excluded.updated_at WHERE excluded.updated_at > topics.updated_at"));
    q.addBindValue(id);
    q.addBindValue(subjectId);
    q.addBindValue(t.name);
    q.addBindValue(t.updatedAt.toUTC().toString(Qt::ISODate));
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0) {
        changed = true;
        clearTombstone(q, "topics", id);
    }
    return true;
}

bool lwwUpsertStrategy(QSqlQuery& q, const PomodoroStrategy& s, bool& changed) {
    const QString id = s.id.toString(QUuid::WithoutBraces);
    q.prepare(QStringLiteral(
        "INSERT INTO strategies(id, name, work_min, break_min, long_break_min, "
        "cycles_before_long, builtin, updated_at) VALUES(?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, work_min = excluded.work_min, "
        "break_min = excluded.break_min, long_break_min = excluded.long_break_min, "
        "cycles_before_long = excluded.cycles_before_long, updated_at = excluded.updated_at "
        "WHERE excluded.updated_at > strategies.updated_at AND strategies.builtin = 0"));
    q.addBindValue(id);
    q.addBindValue(s.name);
    q.addBindValue(s.workMinutes);
    q.addBindValue(s.breakMinutes);
    q.addBindValue(s.longBreakMinutes);
    q.addBindValue(s.cyclesBeforeLongBreak);
    q.addBindValue(s.updatedAt.toUTC().toString(Qt::ISODate));
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0) {
        changed = true;
        clearTombstone(q, "strategies", id);
    }
    return true;
}

bool lwwUpsertEvent(QSqlQuery& q, const CalendarEvent& e, const QHash<QString, QString>& remap,
                    bool& changed) {
    const QString id = e.id.toString(QUuid::WithoutBraces);
    const QString subjectId = remapId(remap, e.subjectId.toString(QUuid::WithoutBraces));
    q.prepare(QStringLiteral(
        "INSERT INTO events(id, provider, external_id, title, description, start_utc, end_utc, "
        "all_day, rrule, subject_id, source_session_id, updated_at, etag) "
        "VALUES(?, 'local', '', ?, ?, ?, ?, ?, ?, ?, ?, ?, '') "
        "ON CONFLICT(id) DO UPDATE SET title = excluded.title, description = excluded.description, "
        "start_utc = excluded.start_utc, end_utc = excluded.end_utc, all_day = excluded.all_day, "
        "rrule = excluded.rrule, subject_id = excluded.subject_id, "
        "source_session_id = excluded.source_session_id, updated_at = excluded.updated_at "
        "WHERE excluded.updated_at > events.updated_at"));
    q.addBindValue(id);
    q.addBindValue(e.title);
    q.addBindValue(e.description);
    q.addBindValue(e.startUtc.toUTC().toString(Qt::ISODate));
    q.addBindValue(e.endUtc.toUTC().toString(Qt::ISODate));
    q.addBindValue(e.allDay ? 1 : 0);
    q.addBindValue(e.rrule);
    q.addBindValue(orNull(subjectId.isEmpty() ? QString() : subjectId));
    q.addBindValue(orNull(e.sourceSessionId.toString(QUuid::WithoutBraces)));
    q.addBindValue(e.updatedAt.toUTC().toString(Qt::ISODate));
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0) {
        changed = true;
        clearTombstone(q, "events", id);
    }
    return true;
}

// Resuelve la referencia al evento de una sesión a un event_id local (o vacío).
QString resolveEventId(QSqlQuery& q, const SessionEventRef& ref) {
    switch (ref.kind) {
    case SessionEventRef::Kind::Local:
        return ref.localId.toString(QUuid::WithoutBraces);
    case SessionEventRef::Kind::Remote: {
        q.prepare(QStringLiteral("SELECT id FROM events WHERE provider = ? AND external_id = ?"));
        q.addBindValue(ref.provider);
        q.addBindValue(ref.externalId);
        if (q.exec() && q.next())
            return q.value(0).toString();
        return QString(); // espejo de Google aún no sincronizado: vínculo colgante tolerado
    }
    case SessionEventRef::Kind::None:
        break;
    }
    return QString();
}

bool lwwUpsertSession(QSqlQuery& q, const StudySession& s, const SessionEventRef& ref,
                      const QHash<QString, QString>& remap, bool& changed) {
    const QString id = s.id.toString(QUuid::WithoutBraces);
    const QString subjectId = remapId(remap, s.subjectId.toString(QUuid::WithoutBraces));
    const QString eventId = resolveEventId(q, ref);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions(id, subject_id, strategy_id, topic, planned_min, actual_sec, "
        "started_at, ended_at, status, event_id, updated_at, "
        "resume_phase_index, resume_phase_elapsed_sec) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET subject_id = excluded.subject_id, "
        "strategy_id = excluded.strategy_id, topic = excluded.topic, "
        "planned_min = excluded.planned_min, actual_sec = excluded.actual_sec, "
        "started_at = excluded.started_at, ended_at = excluded.ended_at, status = excluded.status, "
        "event_id = excluded.event_id, updated_at = excluded.updated_at, "
        "resume_phase_index = excluded.resume_phase_index, "
        "resume_phase_elapsed_sec = excluded.resume_phase_elapsed_sec "
        "WHERE excluded.updated_at > sessions.updated_at"));
    q.addBindValue(id);
    q.addBindValue(orNull(subjectId));
    q.addBindValue(orNull(s.strategyId.toString(QUuid::WithoutBraces)));
    q.addBindValue(s.topic);
    q.addBindValue(s.plannedMinutes);
    q.addBindValue(s.actualSeconds);
    q.addBindValue(orNull(s.startedAt.isValid() ? s.startedAt.toUTC().toString(Qt::ISODate)
                                                : QString()));
    q.addBindValue(orNull(s.endedAt.isValid() ? s.endedAt.toUTC().toString(Qt::ISODate)
                                              : QString()));
    q.addBindValue(sessionStatusToString(s.status));
    q.addBindValue(orNull(eventId));
    q.addBindValue(s.updatedAt.toUTC().toString(Qt::ISODate));
    // Progreso de reanudación: NULL si no es retomable.
    if (s.resumePhaseIndex < 0) {
        q.addBindValue(QVariant());
        q.addBindValue(QVariant());
    } else {
        q.addBindValue(s.resumePhaseIndex);
        q.addBindValue(s.resumeElapsedSec);
    }
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0) {
        changed = true;
        clearTombstone(q, "sessions", id);
    }
    // Reparación de vínculo: si el espejo de Google ya está disponible y la fila
    // quedó sin event_id en un import anterior, re-resuélvelo sin tocar updated_at.
    if (!eventId.isEmpty()) {
        q.prepare(QStringLiteral("UPDATE sessions SET event_id = ? "
                                 "WHERE id = ? AND (event_id IS NULL OR event_id = '')"));
        q.addBindValue(eventId);
        q.addBindValue(id);
        if (q.exec() && q.numRowsAffected() > 0)
            changed = true;
    }
    return true;
}

// Aplica una lápida: borra solo si deleted_at > updated_at local (anti-pérdida).
// Anula las referencias dependientes y respeta las estrategias builtin.
bool applyTombstone(QSqlQuery& q, const TombstoneRecord& t, bool& changed) {
    static const QHash<QString, QString> table = {
        {QStringLiteral("subjects"), QStringLiteral("subjects")},
        {QStringLiteral("topics"), QStringLiteral("topics")},
        {QStringLiteral("strategies"), QStringLiteral("strategies")},
        {QStringLiteral("sessions"), QStringLiteral("sessions")},
        {QStringLiteral("events"), QStringLiteral("events")}};
    const auto it = table.constFind(t.entity);
    if (it == table.constEnd())
        return true; // entidad desconocida: se ignora (no es SQL inyectable)
    const QString tbl = it.value();
    const QString id = t.id.toString(QUuid::WithoutBraces);

    q.prepare(QStringLiteral("SELECT updated_at FROM %1 WHERE id = ?").arg(tbl));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        const QDateTime localUpdated = isoToUtc(q.value(0).toString());
        if (localUpdated.isValid() && localUpdated >= t.deletedAt)
            return true; // la fila local es igual o más nueva: sobrevive y se re-publica
    }

    if (t.entity == QStringLiteral("subjects")) {
        q.prepare(QStringLiteral("UPDATE sessions SET subject_id = NULL WHERE subject_id = ?"));
        q.addBindValue(id);
        q.exec();
        q.prepare(QStringLiteral("UPDATE events SET subject_id = NULL WHERE subject_id = ?"));
        q.addBindValue(id);
        q.exec();
        // Los temas pertenecen a la asignatura: su borrado se arrastra (la FK
        // NOT NULL no admite huérfanos). El borrado remoto ya trae sus propias
        // lápidas de tema; esto es la red de seguridad local.
        q.prepare(QStringLiteral("DELETE FROM topics WHERE subject_id = ?"));
        q.addBindValue(id);
        q.exec();
    } else if (t.entity == QStringLiteral("strategies")) {
        q.prepare(QStringLiteral("UPDATE sessions SET strategy_id = NULL WHERE strategy_id = ?"));
        q.addBindValue(id);
        q.exec();
    } else if (t.entity == QStringLiteral("events")) {
        q.prepare(QStringLiteral("UPDATE sessions SET event_id = NULL WHERE event_id = ?"));
        q.addBindValue(id);
        q.exec();
    }

    q.prepare(t.entity == QStringLiteral("strategies")
                  ? QStringLiteral("DELETE FROM strategies WHERE id = ? AND builtin = 0")
                  : QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(tbl));
    q.addBindValue(id);
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0)
        changed = true;
    return true;
}

// Construye el subject canónico ante una colisión de nombre (UNIQUE): gana el
// UUID lexicográficamente menor; remapea FKs y borra el perdedor.
Subject canonicalSubject(QSqlQuery& q, const Subject& r, QHash<QString, QString>& remap,
                         bool& changed) {
    const QString rId = r.id.toString(QUuid::WithoutBraces);
    q.prepare(QStringLiteral("SELECT id, color, archived, updated_at FROM subjects "
                             "WHERE name = ? AND id <> ?"));
    q.addBindValue(r.name);
    q.addBindValue(rId);
    if (!q.exec() || !q.next())
        return r; // sin colisión

    const QString lId = q.value(0).toString();
    const QString lColor = q.value(1).toString();
    const bool lArchived = q.value(2).toBool();
    const QDateTime lUpdated = isoToUtc(q.value(3).toString());

    const bool rWins = rId < lId;
    const QString winner = rWins ? rId : lId;
    const QString loser = rWins ? lId : rId;
    const bool rNewer = r.updatedAt > lUpdated;

    Subject c;
    c.id = QUuid::fromString(winner);
    c.name = r.name;
    c.colorHex = rNewer ? r.colorHex : lColor;
    c.archived = rNewer ? r.archived : lArchived;
    c.updatedAt = rNewer ? r.updatedAt : lUpdated;

    q.prepare(QStringLiteral("UPDATE sessions SET subject_id = ? WHERE subject_id = ?"));
    q.addBindValue(winner);
    q.addBindValue(loser);
    q.exec();
    q.prepare(QStringLiteral("UPDATE events SET subject_id = ? WHERE subject_id = ?"));
    q.addBindValue(winner);
    q.addBindValue(loser);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM subjects WHERE id = ?"));
    q.addBindValue(loser);
    q.exec();

    remap.insert(loser, winner);
    changed = true;
    return c;
}

// Resuelve una colisión de UNIQUE(subject_id, name) entre temas de dos
// dispositivos: gana el UUID lexicográficamente menor (igual criterio que las
// asignaturas) y borra el perdedor. Los temas no tienen FKs entrantes todavía,
// así que no hace falta remapear referencias.
Topic canonicalTopic(QSqlQuery& q, const Topic& r, const QHash<QString, QString>& remap,
                     bool& changed) {
    Topic c = r;
    c.subjectId = QUuid::fromString(remapId(remap, r.subjectId.toString(QUuid::WithoutBraces)));
    const QString rId = r.id.toString(QUuid::WithoutBraces);
    q.prepare(QStringLiteral("SELECT id, updated_at FROM topics "
                             "WHERE subject_id = ? AND name = ? AND id <> ?"));
    q.addBindValue(c.subjectId.toString(QUuid::WithoutBraces));
    q.addBindValue(r.name);
    q.addBindValue(rId);
    if (!q.exec() || !q.next())
        return c; // sin colisión

    const QString lId = q.value(0).toString();
    const QDateTime lUpdated = isoToUtc(q.value(1).toString());
    const bool rWins = rId < lId;
    c.id = QUuid::fromString(rWins ? rId : lId);
    c.updatedAt = r.updatedAt > lUpdated ? r.updatedAt : lUpdated;

    q.prepare(QStringLiteral("DELETE FROM topics WHERE id = ?"));
    q.addBindValue(rWins ? lId : rId);
    q.exec();
    changed = true;
    return c;
}

bool applyParsed(QSqlDatabase& db, const ParsedData& parsed, bool& changed) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA defer_foreign_keys = ON")))
        return false;

    QHash<QString, QString> remap; // subject loser-id -> winner-id

    for (const Subject& s : parsed.subjects) {
        const Subject canonical = canonicalSubject(q, s, remap, changed);
        if (!lwwUpsertSubject(q, canonical, changed))
            return false;
    }
    for (const Topic& t : parsed.topics) {
        const Topic canonical = canonicalTopic(q, t, remap, changed);
        if (!lwwUpsertTopic(q, canonical, remap, changed))
            return false;
    }
    for (const PomodoroStrategy& s : parsed.strategies) {
        if (!lwwUpsertStrategy(q, s, changed))
            return false;
    }
    for (const CalendarEvent& e : parsed.events) {
        if (!lwwUpsertEvent(q, e, remap, changed))
            return false;
    }
    for (const auto& [s, ref] : parsed.sessions) {
        if (!lwwUpsertSession(q, s, ref, remap, changed))
            return false;
    }
    for (const TombstoneRecord& t : parsed.tombstones) {
        if (!applyTombstone(q, t, changed))
            return false;
    }

    // Red de seguridad: anula FKs que hayan quedado colgando (referente borrado o
    // ausente) para que el commit no falle por integridad referencial.
    if (!q.exec(QStringLiteral("UPDATE sessions SET subject_id = NULL WHERE subject_id IS NOT NULL "
                               "AND subject_id NOT IN (SELECT id FROM subjects)")))
        return false;
    if (!q.exec(QStringLiteral("UPDATE sessions SET strategy_id = NULL WHERE strategy_id IS NOT NULL "
                               "AND strategy_id NOT IN (SELECT id FROM strategies)")))
        return false;
    // Un tema cuya asignatura ya no existe (borrada o aún no sincronizada) sería
    // un huérfano que rompería la FK NOT NULL en el commit: se descarta.
    if (!q.exec(QStringLiteral(
            "DELETE FROM topics WHERE subject_id NOT IN (SELECT id FROM subjects)")))
        return false;
    return true;
}

} // namespace

SyncImporter::SyncImporter(QSqlDatabase db, QString repoDir)
    : m_db(std::move(db)), m_repoDir(std::move(repoDir)) {}

SyncImporter::Result SyncImporter::importAll() {
    Result r;
    if (!manifestSupported(m_repoDir, r.error))
        return r;

    ParsedData parsed;
    for (const QString& entity : kEntities) {
        QDir dir(m_repoDir + QStringLiteral("/data/") + entity);
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
        for (const QFileInfo& fi : files)
            categorize(entity, fi.completeBaseName(), fi.absoluteFilePath(), parsed);
    }

    if (!m_db.transaction()) {
        r.error = QStringLiteral("No se pudo iniciar la transacción de importación.");
        return r;
    }
    if (!applyParsed(m_db, parsed, r.applied)) {
        m_db.rollback();
        r.error = QStringLiteral("Error al importar los datos sincronizados.");
        return r;
    }
    if (!m_db.commit()) {
        m_db.rollback();
        r.error = QStringLiteral("No se pudo confirmar la importación.");
        return r;
    }
    r.ok = true;
    return r;
}

SyncImporter::Result SyncImporter::importPaths(const QStringList& relPaths) {
    Result r;
    if (!manifestSupported(m_repoDir, r.error))
        return r;

    ParsedData parsed;
    bool any = false;
    for (const QString& rel : relPaths) {
        QString entity;
        QString stem;
        if (!parseDataPath(rel, entity, stem))
            continue;
        any = true;
        categorize(entity, stem, m_repoDir + QLatin1Char('/') + rel, parsed);
    }
    if (!any) {
        r.ok = true; // nada relevante que importar
        return r;
    }

    if (!m_db.transaction()) {
        r.error = QStringLiteral("No se pudo iniciar la transacción de importación.");
        return r;
    }
    if (!applyParsed(m_db, parsed, r.applied)) {
        m_db.rollback();
        r.error = QStringLiteral("Error al importar los datos sincronizados.");
        return r;
    }
    if (!m_db.commit()) {
        m_db.rollback();
        r.error = QStringLiteral("No se pudo confirmar la importación.");
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace pass::sync
