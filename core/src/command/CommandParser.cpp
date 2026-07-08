// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/command/CommandParser.h"

#include <QChar>

namespace pass::command {

QString actionToString(Action action) {
    switch (action) {
    case Action::Create:
        return QStringLiteral("create");
    }
    return {};
}

QString entityToString(Entity entity) {
    switch (entity) {
    case Entity::Note:
        return QStringLiteral("note");
    case Entity::Task:
        return QStringLiteral("task");
    case Entity::Event:
        return QStringLiteral("event");
    case Entity::Session:
        return QStringLiteral("session");
    case Entity::Subject:
        return QStringLiteral("subject");
    case Entity::Topic:
        return QStringLiteral("topic");
    }
    return {};
}

std::optional<Action> actionFromString(const QString& token) {
    const QString t = token.toLower();
    if (t == QStringLiteral("create"))
        return Action::Create;
    return std::nullopt;
}

std::optional<Entity> entityFromString(const QString& token) {
    const QString t = token.toLower();
    if (t == QStringLiteral("note"))
        return Entity::Note;
    if (t == QStringLiteral("task"))
        return Entity::Task;
    if (t == QStringLiteral("event"))
        return Entity::Event;
    if (t == QStringLiteral("session"))
        return Entity::Session;
    if (t == QStringLiteral("subject"))
        return Entity::Subject;
    if (t == QStringLiteral("topic"))
        return Entity::Topic;
    return std::nullopt;
}

namespace {

// Tokenizador estilo shell minimal. Divide por espacios respetando comillas
// dobles; dentro de ellas '\\' escapa '"' y '\\' (cualquier otro '\\' queda como
// literal). Sin expansión de variables ni operadores de shell. Devuelve false en
// `ok` (y mensaje en `error`) si hay comillas sin cerrar.
bool tokenize(const QString& text, QList<QString>& out, QString& error) {
    out.clear();
    QString cur;
    bool inQuotes = false;
    bool hasToken = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);

        if (inQuotes) {
            if (c == QLatin1Char('\\')) {
                if (i + 1 < text.size()) {
                    const QChar next = text.at(i + 1);
                    if (next == QLatin1Char('"') || next == QLatin1Char('\\')) {
                        cur.append(next);
                        ++i;
                        continue;
                    }
                }
                cur.append(c); // backslash literal si no es un escape reconocido
                continue;
            }
            if (c == QLatin1Char('"')) {
                inQuotes = false;
                continue;
            }
            cur.append(c);
            continue;
        }

        if (c == QLatin1Char('"')) {
            inQuotes = true;
            hasToken = true;
            continue;
        }
        if (c.isSpace()) {
            if (hasToken) {
                out << cur;
                cur.clear();
                hasToken = false;
            }
            continue;
        }
        cur.append(c);
        hasToken = true;
    }

    if (inQuotes) {
        error = QStringLiteral("comillas sin cerrar");
        return false;
    }
    if (hasToken)
        out << cur;
    return true;
}

} // namespace

QSet<QString> valueFlagsFor(Entity entity) {
    switch (entity) {
    case Entity::Subject:
        return {QStringLiteral("color")};
    case Entity::Topic:
        return {QStringLiteral("subject")};
    case Entity::Note:
        return {QStringLiteral("subject"), QStringLiteral("body")};
    case Entity::Event:
        return {QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("subject"),
                QStringLiteral("desc")};
    case Entity::Task:
        return {QStringLiteral("due"), QStringLiteral("subject"), QStringLiteral("desc")};
    case Entity::Session:
        return {QStringLiteral("start"), QStringLiteral("minutes"), QStringLiteral("subject"),
                QStringLiteral("topic"), QStringLiteral("strategy")};
    }
    return {};
}

QSet<QString> boolFlagsFor(Entity entity) {
    switch (entity) {
    case Entity::Event:
        return {QStringLiteral("all-day")};
    case Entity::Subject:
    case Entity::Topic:
    case Entity::Note:
    case Entity::Task:
    case Entity::Session:
        return {};
    }
    return {};
}

ParseResult parse(const QString& text, const QString& sourceFile) {
    ParseResult result;

    QList<QString> tokens;
    QString tokError;
    if (!tokenize(text, tokens, tokError)) {
        result.error = tokError;
        return result;
    }
    if (tokens.size() < 3) {
        result.error = QStringLiteral("formato esperado: Pass create <entidad> ...");
        return result;
    }

    // tokens[0] = "Pass" (literal de arranque, no se valida más allá de su
    // presencia: es la marca que identifica la línea como comando de Pass).
    const QString actionToken = tokens.at(1).toLower();
    const auto action = actionFromString(actionToken);
    if (!action || *action != Action::Create) {
        result.error = QStringLiteral("acción no soportada ('%1'); use 'create'").arg(tokens.at(1));
        return result;
    }

    const auto entity = entityFromString(tokens.at(2).toLower());
    if (!entity) {
        result.error = QStringLiteral("entidad desconocida: '%1'").arg(tokens.at(2));
        return result;
    }

    Command cmd;
    cmd.action = *action;
    cmd.entity = *entity;
    cmd.rawText = text;
    cmd.sourceFile = sourceFile;

    const QSet<QString> valueFlags = valueFlagsFor(*entity);
    const QSet<QString> boolFlags = boolFlagsFor(*entity);

    for (int i = 3; i < tokens.size(); ++i) {
        const QString tok = tokens.at(i);

        if (tok.startsWith(QStringLiteral("--"))) {
            QString body = tok.mid(2);
            QString flagName;
            QString flagValue;
            bool hasInlineValue = false;
            const int eq = body.indexOf(QLatin1Char('='));
            if (eq >= 0) {
                flagName = body.left(eq).toLower();
                flagValue = body.mid(eq + 1);
                hasInlineValue = true;
            } else {
                flagName = body.toLower();
            }

            if (boolFlags.contains(flagName)) {
                if (hasInlineValue) {
                    result.error = QStringLiteral("el flag --%1 no admite valor").arg(flagName);
                    return result;
                }
                cmd.boolFlags.insert(flagName);
                continue;
            }
            if (valueFlags.contains(flagName)) {
                if (!hasInlineValue) {
                    if (i + 1 >= tokens.size()) {
                        result.error = QStringLiteral("el flag --%1 requiere un valor").arg(flagName);
                        return result;
                    }
                    flagValue = tokens.at(++i);
                }
                cmd.flags.insert(flagName, flagValue);
                continue;
            }

            result.error = QStringLiteral("flag desconocido: --%1").arg(flagName);
            return result;
        }

        // Token posicional (puede ser la última forma de pasar un valor sin
        // flags, p. ej. el título del evento). Se acumula en orden.
        cmd.positional << tok;
    }

    result.ok = true;
    result.command = std::move(cmd);
    return result;
}

} // namespace pass::command
