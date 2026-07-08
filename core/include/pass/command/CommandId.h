// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/command/CommandTypes.h"

#include <QUuid>

namespace pass::command {

// UUIDv5 determinista del texto canónico del comando. Procesar el mismo comando
// dos veces (o en dos dispositivos) produce EXACTAMENTE el mismo id, de modo que
// el last-writer-wins de la sync lo deduplica sin esfuerzo: un reproceso no crea
// un duplicado, solo reescribe el mismo registro con un updated_at más nuevo.
//
// Para notas (que se identifican por nombre de fichero, no por UUID) este id se
// embebe en el frontmatter (`pass_command_id`) y sirve de marca de idempotencia.
QUuid deterministicId(const Command& cmd);

// Variante con "sal": útil cuando un mismo comando materializa en varios recursos
// que necesitan ids distintos pero estables (p. ej. `create session` deriva un id
// para la sesión y otro, con sal "event", para el evento enlazado). El mismo
// comando + la misma sal siempre producen el mismo UUID.
QUuid deterministicIdFor(const Command& cmd, const QString& salt);

} // namespace pass::command
