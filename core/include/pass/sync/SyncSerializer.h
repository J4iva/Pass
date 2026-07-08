// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"
#include "pass/domain/PomodoroStrategy.h"
#include "pass/domain/StudySession.h"
#include "pass/domain/Subject.h"
#include "pass/domain/Topic.h"

#include <QJsonObject>
#include <QString>
#include <QUuid>

// Serialización pura entidad <-> QJsonObject para el espejo de sincronización.
// Sin acceso a BD ni a disco: las funciones from* validan la entrada (tipos,
// campos obligatorios) y devuelven false ante un JSON corrupto o malicioso;
// los campos desconocidos se ignoran. Fechas en ISO 8601 UTC; las claves salen
// ordenadas (QJsonObject las mantiene ordenadas) para diffs deterministas.
namespace pass::sync {

inline constexpr int kManifestFormat = 1; // versión del formato del espejo

// Referencia al evento vinculado de una sesión, tal como viaja en el JSON.
// Un evento local se referencia por su UUID (igual en todos los dispositivos);
// un espejo de Google se referencia por (provider, external_id), porque su UUID
// local difiere en cada máquina y se resuelve al importar con byExternalId.
struct SessionEventRef {
    enum class Kind { None, Local, Remote };
    Kind kind = Kind::None;
    QUuid localId;       // Kind::Local
    QString provider;    // Kind::Remote (p. ej. "google")
    QString externalId;  // Kind::Remote
};

// Lápida (borrado explícito) de una entidad.
struct TombstoneRecord {
    QString entity; // "subjects" | "strategies" | "sessions" | "events" | "topics"
    QUuid id;
    QDateTime deletedAt; // UTC
};

QJsonObject toJson(const Subject& s);
bool fromJson(const QJsonObject& obj, Subject& out);

QJsonObject toJson(const Topic& t);
bool fromJson(const QJsonObject& obj, Topic& out);

QJsonObject toJson(const PomodoroStrategy& s);
bool fromJson(const QJsonObject& obj, PomodoroStrategy& out);

// Asume un evento puramente local (provider="local", sin external_id/etag).
QJsonObject toJson(const CalendarEvent& e);
bool fromJson(const QJsonObject& obj, CalendarEvent& out);

QJsonObject toJson(const StudySession& s, const SessionEventRef& ref);
bool fromJson(const QJsonObject& obj, StudySession& out, SessionEventRef& outRef);

QJsonObject toJson(const TombstoneRecord& t);
bool fromJson(const QJsonObject& obj, TombstoneRecord& out);

} // namespace pass::sync
