// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QUuid>

namespace pass {

// Nombres de entidad compartidos por los tombstones y el espejo data/<entity>/.
// Mantenerlos sincronizados con las subcarpetas que escribe SyncExporter.
namespace entity {
inline constexpr auto kSubjects = "subjects";
inline constexpr auto kStrategies = "strategies";
inline constexpr auto kSessions = "sessions";
inline constexpr auto kEvents = "events";
inline constexpr auto kTopics = "topics";
} // namespace entity

// Inserta (o refresca) el tombstone (entity, id) con deleted_at = ahora UTC.
// Pensado para ejecutarse dentro de la misma transacción que el DELETE de la
// fila, de modo que borrado y lápida sean atómicos. Devuelve false si falla.
bool insertTombstone(QSqlDatabase& db, const char* entity, const QUuid& id);

} // namespace pass
