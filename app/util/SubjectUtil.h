// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QString>
#include <QUuid>

namespace util {

// Color estable de la paleta para una asignatura dada.
QString colorForName(const QString& name);

// Devuelve el id de la asignatura con ese nombre, creándola si no existe.
// Nulo si el nombre está vacío o la creación falla.
QUuid ensureSubject(pass::SubjectRepository& repo, const QString& name);

// Devuelve el id del tema con ese nombre dentro de la asignatura, creándolo si
// no existe. Nulo si el nombre o la asignatura están vacíos o la creación falla.
QUuid ensureTopic(pass::TopicRepository& repo, const QUuid& subjectId, const QString& name);

} // namespace util
