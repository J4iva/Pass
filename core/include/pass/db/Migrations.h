// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QUuid>

namespace pass {

// Versión de esquema almacenada en PRAGMA user_version.
int schemaVersion(QSqlDatabase db);

// Aplica en transacción cada migración pendiente. Idempotente.
// Nunca editar una migración ya publicada: añadir una nueva.
bool applyMigrations(QSqlDatabase db);

// Id determinista (UUIDv5) de una estrategia builtin a partir de su nombre.
// Es igual en todos los dispositivos, para que las sesiones sincronizadas
// referencien la misma estrategia tras importarse en otra máquina.
QUuid builtinStrategyId(const QString& name);

} // namespace pass
