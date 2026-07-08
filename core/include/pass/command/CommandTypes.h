// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include <optional>

namespace pass::command {

// CLI "retransmitido" por el repo de sync: el usuario escribe comandos en texto
// (estilo `Pass create note "..." --subject Cálculo`) en `command/*.passcmd` del
// repo; Pass los aplica automáticamente tras cada pull (y al arrancar la app),
// reutilizando la lógica del core. Es una capa de azúcar sobre el formato del
// repo de sync (ver docs/passport-integration.md): el writer no necesita generar
// JSON/UUID a mano.
//
// MVP: SOLO creación (Action::Create). Nunca borrado. Editar queda reservado.

enum class Action { Create };

enum class Entity { Note, Task, Event, Session, Subject, Topic };

QString actionToString(Action action);
QString entityToString(Entity entity);
std::optional<Action> actionFromString(const QString& token);
std::optional<Entity> entityFromString(const QString& token);

// Estado con el que termina un comando al aplicarlo.
enum class CommandStatus {
    Applied, // se creó el recurso
    Skipped, // no hacía falta (idempotente: ya existía con el mismo id/comando)
    Failed   // validación semántica fallida; no se aplica ni se mueve a processed/
};

// Un comando ya parseado. Inmutable tras parse().
struct Command {
    Action action = Action::Create;
    Entity entity = Entity::Note;
    QList<QString> positional;      // tokens posicionales (title / name / topic...)
    QMap<QString, QString> flags;   // --flag valor (flags que admiten valor)
    QSet<QString> boolFlags;        // flags booleanos presentes (p. ej. --all-day)
    QString rawText;                // texto crudo del fichero, base del UUIDv5
    QString sourceFile;             // basename del .passcmd (solo para trazas)
};

struct CommandResult {
    CommandStatus status = CommandStatus::Failed;
    QString message; // humano: descripción de qué se creó / por qué se omitió / error
};

} // namespace pass::command
