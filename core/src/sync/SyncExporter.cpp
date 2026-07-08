// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/SyncExporter.h"

#include "pass/sync/SyncSerializer.h"
#include "pass/sync/Tombstones.h"

#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlQuery>
#include <QTimeZone>

namespace pass::sync {

namespace {

QDateTime isoToUtc(const QString& text) {
    QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

QByteArray toBytes(const QJsonObject& obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}

// Escribe el archivo solo si su contenido difiere (mantiene git status limpio y
// los diffs deterministas). Devuelve false ante un error de E/S.
bool writeIfDiffers(const QString& path, const QByteArray& bytes) {
    QFile existing(path);
    if (existing.exists() && existing.open(QIODevice::ReadOnly)) {
        const QByteArray current = existing.readAll();
        existing.close();
        if (current == bytes)
            return true;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    if (out.write(bytes) != bytes.size())
        return false;
    return out.commit();
}

// Sincroniza un directorio con el conjunto de archivos deseado: escribe/actualiza
// cada <stem>.json y borra los .json sobrantes (filas que ya no existen).
bool syncDir(const QString& dirPath, const QHash<QString, QByteArray>& files) {
    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath))
        return false;
    const QFileInfoList existing =
        dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo& fi : existing) {
        if (!files.contains(fi.completeBaseName()) && !dir.remove(fi.fileName()))
            return false;
    }
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (!writeIfDiffers(dir.filePath(it.key() + QStringLiteral(".json")), it.value()))
            return false;
    }
    return true;
}

} // namespace

SyncExporter::SyncExporter(QSqlDatabase db, QString repoDir)
    : m_db(std::move(db)), m_repoDir(std::move(repoDir)) {}

bool SyncExporter::exportAll() {
    const QString dataDir = m_repoDir + QStringLiteral("/data");
    if (!QDir().mkpath(dataDir))
        return false;

    // manifest.json: guard de compatibilidad del formato.
    {
        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), kManifestFormat);
        if (!writeIfDiffers(m_repoDir + QStringLiteral("/manifest.json"), toBytes(manifest)))
            return false;
    }

    QSqlQuery q(m_db);

    // Subjects.
    {
        QHash<QString, QByteArray> files;
        if (!q.exec(QStringLiteral("SELECT id, name, color, archived, updated_at FROM subjects")))
            return false;
        while (q.next()) {
            Subject s;
            s.id = QUuid::fromString(q.value(0).toString());
            s.name = q.value(1).toString();
            s.colorHex = q.value(2).toString();
            s.archived = q.value(3).toBool();
            s.updatedAt = isoToUtc(q.value(4).toString());
            files.insert(s.id.toString(QUuid::WithoutBraces), toBytes(toJson(s)));
        }
        if (!syncDir(dataDir + QStringLiteral("/subjects"), files))
            return false;
    }

    // Topics.
    {
        QHash<QString, QByteArray> files;
        if (!q.exec(QStringLiteral("SELECT id, subject_id, name, updated_at FROM topics")))
            return false;
        while (q.next()) {
            Topic t;
            t.id = QUuid::fromString(q.value(0).toString());
            t.subjectId = QUuid::fromString(q.value(1).toString());
            t.name = q.value(2).toString();
            t.updatedAt = isoToUtc(q.value(3).toString());
            files.insert(t.id.toString(QUuid::WithoutBraces), toBytes(toJson(t)));
        }
        if (!syncDir(dataDir + QStringLiteral("/topics"), files))
            return false;
    }

    // Strategies (solo personalizadas).
    {
        QHash<QString, QByteArray> files;
        if (!q.exec(QStringLiteral(
                "SELECT id, name, work_min, break_min, long_break_min, cycles_before_long, "
                "updated_at FROM strategies WHERE builtin = 0")))
            return false;
        while (q.next()) {
            PomodoroStrategy s;
            s.id = QUuid::fromString(q.value(0).toString());
            s.name = q.value(1).toString();
            s.workMinutes = q.value(2).toInt();
            s.breakMinutes = q.value(3).toInt();
            s.longBreakMinutes = q.value(4).toInt();
            s.cyclesBeforeLongBreak = q.value(5).toInt();
            s.updatedAt = isoToUtc(q.value(6).toString());
            files.insert(s.id.toString(QUuid::WithoutBraces), toBytes(toJson(s)));
        }
        if (!syncDir(dataDir + QStringLiteral("/strategies"), files))
            return false;
    }

    // Events (solo locales, sin external_id).
    {
        QHash<QString, QByteArray> files;
        if (!q.exec(QStringLiteral(
                "SELECT id, title, description, start_utc, end_utc, all_day, rrule, subject_id, "
                "source_session_id, updated_at FROM events "
                "WHERE provider = 'local' AND (external_id IS NULL OR external_id = '')")))
            return false;
        while (q.next()) {
            CalendarEvent e;
            e.id = QUuid::fromString(q.value(0).toString());
            e.title = q.value(1).toString();
            e.description = q.value(2).toString();
            e.startUtc = isoToUtc(q.value(3).toString());
            e.endUtc = isoToUtc(q.value(4).toString());
            e.allDay = q.value(5).toBool();
            e.rrule = q.value(6).toString();
            e.subjectId = QUuid::fromString(q.value(7).toString());
            e.sourceSessionId = QUuid::fromString(q.value(8).toString());
            e.updatedAt = isoToUtc(q.value(9).toString());
            files.insert(e.id.toString(QUuid::WithoutBraces), toBytes(toJson(e)));
        }
        if (!syncDir(dataDir + QStringLiteral("/events"), files))
            return false;
    }

    // Sessions (con resolución de la referencia al evento vinculado).
    {
        QHash<QString, QByteArray> files;
        if (!q.exec(QStringLiteral(
                "SELECT s.id, s.subject_id, s.strategy_id, s.topic, s.planned_min, s.actual_sec, "
                "s.started_at, s.ended_at, s.status, s.updated_at, e.provider, e.external_id, "
                "s.event_id, s.resume_phase_index, s.resume_phase_elapsed_sec "
                "FROM sessions s LEFT JOIN events e ON e.id = s.event_id")))
            return false;
        while (q.next()) {
            StudySession s;
            s.id = QUuid::fromString(q.value(0).toString());
            s.subjectId = QUuid::fromString(q.value(1).toString());
            s.strategyId = QUuid::fromString(q.value(2).toString());
            s.topic = q.value(3).toString();
            s.plannedMinutes = q.value(4).toInt();
            s.actualSeconds = q.value(5).toInt();
            s.startedAt = isoToUtc(q.value(6).toString());
            s.endedAt = isoToUtc(q.value(7).toString());
            s.status = sessionStatusFromString(q.value(8).toString());
            s.updatedAt = isoToUtc(q.value(9).toString());
            s.resumePhaseIndex = q.value(13).isNull() ? -1 : q.value(13).toInt();
            s.resumeElapsedSec = q.value(14).isNull() ? 0 : q.value(14).toInt();

            SessionEventRef ref;
            const QUuid eventId = QUuid::fromString(q.value(12).toString());
            if (!eventId.isNull()) {
                const QString provider = q.value(10).toString();
                const QString externalId = q.value(11).toString();
                if (provider.isEmpty() || externalId.isEmpty()) {
                    // Evento local (o referencia colgante): se referencia por UUID.
                    ref.kind = SessionEventRef::Kind::Local;
                    ref.localId = eventId;
                } else {
                    // Espejo de Google: por (provider, external_id), portable.
                    ref.kind = SessionEventRef::Kind::Remote;
                    ref.provider = provider;
                    ref.externalId = externalId;
                }
            }
            files.insert(s.id.toString(QUuid::WithoutBraces), toBytes(toJson(s, ref)));
        }
        if (!syncDir(dataDir + QStringLiteral("/sessions"), files))
            return false;
    }

    // Tombstones (borrados explícitos; nunca se eliminan del espejo).
    {
        QHash<QString, QByteArray> files;
        if (!q.exec(QStringLiteral("SELECT entity, id, deleted_at FROM tombstones")))
            return false;
        while (q.next()) {
            TombstoneRecord t;
            t.entity = q.value(0).toString();
            t.id = QUuid::fromString(q.value(1).toString());
            t.deletedAt = isoToUtc(q.value(2).toString());
            files.insert(t.id.toString(QUuid::WithoutBraces), toBytes(toJson(t)));
        }
        if (!syncDir(dataDir + QStringLiteral("/tombstones"), files))
            return false;
    }

    return true;
}

} // namespace pass::sync
