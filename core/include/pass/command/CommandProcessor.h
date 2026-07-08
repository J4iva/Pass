// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/command/CommandTypes.h"

#include <QSqlDatabase>
#include <QString>

namespace pass::command {

// Aplica un comando ya parseado mutando la BD (y, para `create note`, el espejo
// de notas del repo). Reutiliza los repositorios del core: NO reescribe lógica de
// negocio. El export del repo de sync publica después lo insertado aquí.
//
// Idempotencia (modo contingencia): el id de cada recurso se deriva del texto del
// comando vía UUIDv5 (CommandId). Reprocesar el mismo comando produce el mismo id
// y, por tanto, se resuelve como Skipped (no duplicado); el last-writer-wins de
// la sync lo deduplica entre dispositivos. La resolución por nombre (subject/
// topic ya existentes) también se trata como Skipped, nunca como error.
//
// Validación: todo fallo semántico (subject inexistente, fecha inválida, falta un
// argumento obligatorio) se devuelve como Failed CON un mensaje; NUNCA lanza. El
// llamador decide si reintentar/mover a processed (Applied/Skipped) o dejar en
// sitio (Failed).
class CommandProcessor {
public:
    CommandProcessor(QSqlDatabase db, QString repoDir);

    CommandResult process(const Command& cmd);

private:
    CommandResult processSubject(const Command& cmd);
    CommandResult processTopic(const Command& cmd);
    CommandResult processEvent(const Command& cmd);
    CommandResult processTask(const Command& cmd);
    CommandResult processSession(const Command& cmd);
    CommandResult processNote(const Command& cmd);

    QSqlDatabase m_db;
    QString m_repoDir;
};

} // namespace pass::command
