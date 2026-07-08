// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/SyncSerializer.h"

#include <QJsonValue>
#include <QTimeZone>

namespace pass::sync {

namespace {

QString uuidStr(const QUuid& id) {
    return id.toString(QUuid::WithoutBraces);
}

QString isoUtc(const QDateTime& dt) {
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODate) : QString();
}

// Lee un UUID obligatorio: debe ser un string con un UUID válido.
bool readUuid(const QJsonValue& v, QUuid& out) {
    if (!v.isString())
        return false;
    const QUuid id = QUuid::fromString(v.toString());
    if (id.isNull())
        return false;
    out = id;
    return true;
}

// Lee un UUID opcional: ausente o vacío => nulo; si está, debe ser válido.
bool readOptionalUuid(const QJsonObject& obj, const char* key, QUuid& out) {
    const auto it = obj.constFind(QLatin1String(key));
    if (it == obj.constEnd() || it->isNull())
        return true;
    if (!it->isString())
        return false;
    const QString s = it->toString();
    if (s.isEmpty())
        return true;
    const QUuid id = QUuid::fromString(s);
    if (id.isNull())
        return false;
    out = id;
    return true;
}

// Lee una fecha ISO 8601 obligatoria y la fija a UTC.
bool readDateTime(const QJsonValue& v, QDateTime& out) {
    if (!v.isString())
        return false;
    QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODate);
    if (!dt.isValid())
        return false;
    dt.setTimeZone(QTimeZone::utc());
    out = dt;
    return true;
}

bool readString(const QJsonValue& v, QString& out) {
    if (!v.isString())
        return false;
    out = v.toString();
    return true;
}

// Inserta una clave solo si el UUID no es nulo (campos opcionales).
void putOptionalUuid(QJsonObject& obj, const char* key, const QUuid& id) {
    if (!id.isNull())
        obj.insert(QLatin1String(key), uuidStr(id));
}

} // namespace

QJsonObject toJson(const Subject& s) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), uuidStr(s.id));
    o.insert(QStringLiteral("name"), s.name);
    o.insert(QStringLiteral("color"), s.colorHex);
    o.insert(QStringLiteral("archived"), s.archived);
    o.insert(QStringLiteral("updated_at"), isoUtc(s.updatedAt));
    return o;
}

bool fromJson(const QJsonObject& obj, Subject& out) {
    Subject s;
    if (!readUuid(obj.value(QStringLiteral("id")), s.id))
        return false;
    if (!readString(obj.value(QStringLiteral("name")), s.name) || s.name.isEmpty())
        return false;
    // color es opcional (vacío permitido); archived por defecto false.
    s.colorHex = obj.value(QStringLiteral("color")).toString();
    s.archived = obj.value(QStringLiteral("archived")).toBool(false);
    if (!readDateTime(obj.value(QStringLiteral("updated_at")), s.updatedAt))
        return false;
    out = s;
    return true;
}

QJsonObject toJson(const Topic& t) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), uuidStr(t.id));
    o.insert(QStringLiteral("subject_id"), uuidStr(t.subjectId));
    o.insert(QStringLiteral("name"), t.name);
    o.insert(QStringLiteral("updated_at"), isoUtc(t.updatedAt));
    return o;
}

bool fromJson(const QJsonObject& obj, Topic& out) {
    Topic t;
    if (!readUuid(obj.value(QStringLiteral("id")), t.id))
        return false;
    if (!readUuid(obj.value(QStringLiteral("subject_id")), t.subjectId))
        return false;
    if (!readString(obj.value(QStringLiteral("name")), t.name) || t.name.isEmpty())
        return false;
    if (!readDateTime(obj.value(QStringLiteral("updated_at")), t.updatedAt))
        return false;
    out = t;
    return true;
}

QJsonObject toJson(const PomodoroStrategy& s) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), uuidStr(s.id));
    o.insert(QStringLiteral("name"), s.name);
    o.insert(QStringLiteral("work_min"), s.workMinutes);
    o.insert(QStringLiteral("break_min"), s.breakMinutes);
    o.insert(QStringLiteral("long_break_min"), s.longBreakMinutes);
    o.insert(QStringLiteral("cycles_before_long"), s.cyclesBeforeLongBreak);
    o.insert(QStringLiteral("updated_at"), isoUtc(s.updatedAt));
    return o;
}

bool fromJson(const QJsonObject& obj, PomodoroStrategy& out) {
    PomodoroStrategy s;
    if (!readUuid(obj.value(QStringLiteral("id")), s.id))
        return false;
    if (!readString(obj.value(QStringLiteral("name")), s.name) || s.name.isEmpty())
        return false;
    if (!obj.value(QStringLiteral("work_min")).isDouble() ||
        !obj.value(QStringLiteral("break_min")).isDouble() ||
        !obj.value(QStringLiteral("long_break_min")).isDouble() ||
        !obj.value(QStringLiteral("cycles_before_long")).isDouble())
        return false;
    // Acota los enteros a rangos sanos antes de que entren a la BD: el espejo es
    // una frontera no confiable (otro dispositivo o el asistente móvil). Un valor
    // negativo o desorbitado alimentaría el bucle de StrategyCatalog::proposals()
    // o desbordaría el cálculo de fases del timer. El trabajo va a [1,600]
    // (un bloque no puede durar 0); los descansos a [0,600]; los ciclos a [0,100].
    s.workMinutes = qBound(1, obj.value(QStringLiteral("work_min")).toInt(), 600);
    s.breakMinutes = qBound(0, obj.value(QStringLiteral("break_min")).toInt(), 600);
    s.longBreakMinutes = qBound(0, obj.value(QStringLiteral("long_break_min")).toInt(), 600);
    s.cyclesBeforeLongBreak =
        qBound(0, obj.value(QStringLiteral("cycles_before_long")).toInt(), 100);
    s.builtin = false; // el espejo solo contiene estrategias personalizadas
    if (!readDateTime(obj.value(QStringLiteral("updated_at")), s.updatedAt))
        return false;
    out = s;
    return true;
}

QJsonObject toJson(const CalendarEvent& e) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), uuidStr(e.id));
    o.insert(QStringLiteral("title"), e.title);
    o.insert(QStringLiteral("description"), e.description);
    o.insert(QStringLiteral("start_utc"), isoUtc(e.startUtc));
    o.insert(QStringLiteral("end_utc"), isoUtc(e.endUtc));
    o.insert(QStringLiteral("all_day"), e.allDay);
    o.insert(QStringLiteral("rrule"), e.rrule);
    putOptionalUuid(o, "subject_id", e.subjectId);
    putOptionalUuid(o, "source_session_id", e.sourceSessionId);
    o.insert(QStringLiteral("updated_at"), isoUtc(e.updatedAt));
    return o;
}

bool fromJson(const QJsonObject& obj, CalendarEvent& out) {
    CalendarEvent e;
    if (!readUuid(obj.value(QStringLiteral("id")), e.id))
        return false;
    if (!readString(obj.value(QStringLiteral("title")), e.title))
        return false;
    e.description = obj.value(QStringLiteral("description")).toString();
    if (!readDateTime(obj.value(QStringLiteral("start_utc")), e.startUtc))
        return false;
    if (!readDateTime(obj.value(QStringLiteral("end_utc")), e.endUtc))
        return false;
    e.allDay = obj.value(QStringLiteral("all_day")).toBool(false);
    e.rrule = obj.value(QStringLiteral("rrule")).toString();
    if (!readOptionalUuid(obj, "subject_id", e.subjectId))
        return false;
    if (!readOptionalUuid(obj, "source_session_id", e.sourceSessionId))
        return false;
    if (!readDateTime(obj.value(QStringLiteral("updated_at")), e.updatedAt))
        return false;
    e.provider = QStringLiteral("local"); // el espejo solo contiene eventos locales
    out = e;
    return true;
}

QJsonObject toJson(const StudySession& s, const SessionEventRef& ref) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), uuidStr(s.id));
    putOptionalUuid(o, "subject_id", s.subjectId);
    putOptionalUuid(o, "strategy_id", s.strategyId);
    o.insert(QStringLiteral("topic"), s.topic);
    o.insert(QStringLiteral("planned_min"), s.plannedMinutes);
    o.insert(QStringLiteral("actual_sec"), s.actualSeconds);
    o.insert(QStringLiteral("started_at"), isoUtc(s.startedAt));
    o.insert(QStringLiteral("ended_at"), isoUtc(s.endedAt));
    o.insert(QStringLiteral("status"), sessionStatusToString(s.status));
    o.insert(QStringLiteral("updated_at"), isoUtc(s.updatedAt));
    // Progreso de reanudación: solo si la sesión es retomable (opcional/retrocompat).
    if (s.resumePhaseIndex >= 0) {
        o.insert(QStringLiteral("resume_phase_index"), s.resumePhaseIndex);
        o.insert(QStringLiteral("resume_phase_elapsed_sec"), s.resumeElapsedSec);
    }
    switch (ref.kind) {
    case SessionEventRef::Kind::Local:
        o.insert(QStringLiteral("event_id"), uuidStr(ref.localId));
        break;
    case SessionEventRef::Kind::Remote: {
        QJsonObject ev;
        ev.insert(QStringLiteral("provider"), ref.provider);
        ev.insert(QStringLiteral("external_id"), ref.externalId);
        o.insert(QStringLiteral("event"), ev);
        break;
    }
    case SessionEventRef::Kind::None:
        break;
    }
    return o;
}

bool fromJson(const QJsonObject& obj, StudySession& out, SessionEventRef& outRef) {
    StudySession s;
    if (!readUuid(obj.value(QStringLiteral("id")), s.id))
        return false;
    if (!readOptionalUuid(obj, "subject_id", s.subjectId))
        return false;
    if (!readOptionalUuid(obj, "strategy_id", s.strategyId))
        return false;
    s.topic = obj.value(QStringLiteral("topic")).toString();
    s.plannedMinutes = obj.value(QStringLiteral("planned_min")).toInt(0);
    s.actualSeconds = obj.value(QStringLiteral("actual_sec")).toInt(0);
    // started_at/ended_at son opcionales (una sesión planificada puede no tenerlos).
    if (obj.contains(QStringLiteral("started_at")) &&
        !obj.value(QStringLiteral("started_at")).toString().isEmpty() &&
        !readDateTime(obj.value(QStringLiteral("started_at")), s.startedAt))
        return false;
    if (obj.contains(QStringLiteral("ended_at")) &&
        !obj.value(QStringLiteral("ended_at")).toString().isEmpty() &&
        !readDateTime(obj.value(QStringLiteral("ended_at")), s.endedAt))
        return false;
    s.status = sessionStatusFromString(obj.value(QStringLiteral("status")).toString());
    if (!readDateTime(obj.value(QStringLiteral("updated_at")), s.updatedAt))
        return false;
    // Opcionales: ausentes => sesión no retomable (-1 / 0).
    s.resumePhaseIndex = obj.value(QStringLiteral("resume_phase_index")).toInt(-1);
    s.resumeElapsedSec = obj.value(QStringLiteral("resume_phase_elapsed_sec")).toInt(0);

    SessionEventRef ref;
    if (const auto ev = obj.constFind(QStringLiteral("event"));
        ev != obj.constEnd() && ev->isObject()) {
        const QJsonObject eo = ev->toObject();
        ref.kind = SessionEventRef::Kind::Remote;
        ref.provider = eo.value(QStringLiteral("provider")).toString();
        ref.externalId = eo.value(QStringLiteral("external_id")).toString();
        if (ref.provider.isEmpty() || ref.externalId.isEmpty())
            ref.kind = SessionEventRef::Kind::None; // ref incompleta => sin vínculo
    } else if (const auto eid = obj.constFind(QStringLiteral("event_id"));
               eid != obj.constEnd() && eid->isString() && !eid->toString().isEmpty()) {
        QUuid id;
        if (!readUuid(*eid, id))
            return false;
        ref.kind = SessionEventRef::Kind::Local;
        ref.localId = id;
    }
    out = s;
    outRef = ref;
    return true;
}

QJsonObject toJson(const TombstoneRecord& t) {
    QJsonObject o;
    o.insert(QStringLiteral("entity"), t.entity);
    o.insert(QStringLiteral("id"), uuidStr(t.id));
    o.insert(QStringLiteral("deleted_at"), isoUtc(t.deletedAt));
    return o;
}

bool fromJson(const QJsonObject& obj, TombstoneRecord& out) {
    TombstoneRecord t;
    if (!readString(obj.value(QStringLiteral("entity")), t.entity) || t.entity.isEmpty())
        return false;
    if (!readUuid(obj.value(QStringLiteral("id")), t.id))
        return false;
    if (!readDateTime(obj.value(QStringLiteral("deleted_at")), t.deletedAt))
        return false;
    out = t;
    return true;
}

} // namespace pass::sync
