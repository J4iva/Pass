// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/command/CommandId.h"

namespace pass::command {

namespace {

// Namespace fijo (RFC 4122) del que derivar los UUIDv5 de los comandos.
// Deliberadamente DISTINTO del de las estrategias builtin (pass::db) para que un
// comando nunca pueda colisionar con un id existente de otra entidad.
const QUuid kCommandNamespace(QStringLiteral("{8f4c2a6b-3d7e-4a1b-9c5f-2e8d0a1b3c4e}"));

} // namespace

QUuid deterministicIdFor(const Command& cmd, const QString& salt) {
    // Misma canonicalización que deterministicId; la sal se anexa para producir
    // un id distinto (y estable) por cada variante de un mismo comando.
    QString canon = cmd.rawText;
    canon.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    canon.replace(QStringLiteral("\r"), QStringLiteral("\n"));
    canon = canon.trimmed();
    if (!salt.isEmpty())
        canon += QStringLiteral("\n# ") + salt;
    return QUuid::createUuidV5(kCommandNamespace, canon);
}

QUuid deterministicId(const Command& cmd) {
    return deterministicIdFor(cmd, QString());
}

} // namespace pass::command
