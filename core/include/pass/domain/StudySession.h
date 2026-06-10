// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace pass {

enum class SessionStatus { Planned, Completed, Aborted };

QString sessionStatusToString(SessionStatus status);
SessionStatus sessionStatusFromString(const QString& text);

struct StudySession {
    QUuid id;
    QUuid subjectId;  // nulo = sin asignatura
    QUuid strategyId; // nulo = sesión libre
    QString topic;
    int plannedMinutes = 0;
    int actualSeconds = 0;
    QDateTime startedAt; // siempre UTC
    QDateTime endedAt;   // siempre UTC
    SessionStatus status = SessionStatus::Planned;
    QUuid linkedEventId; // evento de calendario asociado, si lo hay
};

} // namespace pass
