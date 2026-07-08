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
    QDateTime updatedAt; // UTC; marca de última escritura (sync entre dispositivos)
    // Progreso para retomar una sesión interrumpida (status Aborted con tiempo
    // restante). resumePhaseIndex = -1 significa "no retomable".
    int resumePhaseIndex = -1; // índice de fase donde se cortó
    int resumeElapsedSec = 0;  // segundos ya consumidos en esa fase
};

} // namespace pass
