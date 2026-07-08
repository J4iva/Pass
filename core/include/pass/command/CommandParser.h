// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/command/CommandTypes.h"

#include <QString>

namespace pass::command {

// Tokenizador + parser estricto del CLI de Pass. Sin acceso a BD ni disco: solo
// valida estructura y sintaxis. La validación semántica (subject existe, fechas
// válidas...) la hace CommandProcessor.
//
// Gramática (MVP, solo `create`):
//   Pass create <entity> [posicionales] [--flag valor | --flag=valor | --bool-flag]*
//
//   entity ∈ {note, task, event, session, subject, topic}
//
//   subject:  <name>                          [--color "#rrggbb"]
//   topic:    <name> --subject <nombre>
//   note:     [<topic>]                        [--subject <nombre>] [--body "..."]
//   event:    <title> --start <ISO> [--end <ISO>] [--all-day] [--subject <n>] [--desc "..."]
//   task:     <title> --due <ISO> --subject <nombre> [--desc "..."]
//   session:  --start <ISO> [--minutes <int>] [--subject <n>] [--topic "..."] [--strategy <id>]
//
//   Fechas ISO-8601. Con sufijo 'Z' = UTC (recomendado). Sin él = hora local del
//   dispositivo que procesa (limitación documentada).
//
// Seguridad (modo contingencia): parser de lista blanca. La acción, la entidad y
// los flags se validan contra conjuntos fijos; cualquier token desconocido se
// rechaza. No hay ejecución de código ni shell: el tokenizador es manual y
// limitado (comillas dobles con escapes '\\' y '\"'; sin expansión de variables
// ni comandos).
struct ParseResult {
    bool ok = false;
    QString error; // mensaje saneado cuando ok=false
    Command command;
};

// Parsea una línea/fichero de comando. `sourceFile` (basename del .passcmd) solo
// se guarda en el Command para trazas; no afecta al parseo.
ParseResult parse(const QString& text, const QString& sourceFile = {});

// Conjuntos de flags válidos por entidad (expuestos para que el processor y los
// tests compartan la misma fuente de verdad).
QSet<QString> valueFlagsFor(Entity entity);
QSet<QString> boolFlagsFor(Entity entity);

} // namespace pass::command
