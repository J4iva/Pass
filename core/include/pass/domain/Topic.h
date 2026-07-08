// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace pass {

// Tema de una asignatura. A diferencia del texto libre `topic` de sesiones y
// notas, es una entidad propia (tabla `topics`) gestionable desde Administración.
struct Topic {
    QUuid id;
    QUuid subjectId;     // asignatura a la que pertenece (obligatoria)
    QString name;
    QDateTime updatedAt; // UTC; marca de última escritura (sync entre dispositivos)
};

} // namespace pass
