// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>

namespace pass {

// Versión de esquema almacenada en PRAGMA user_version.
int schemaVersion(QSqlDatabase db);

// Aplica en transacción cada migración pendiente. Idempotente.
// Nunca editar una migración ya publicada: añadir una nueva.
bool applyMigrations(QSqlDatabase db);

} // namespace pass
