// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QUuid>

namespace pass {

struct SubjectHours {
    QUuid subjectId; // nulo = sin asignatura
    QString subjectName;
    QString colorHex;
    qint64 workSeconds = 0;
};

struct DailyMinutes {
    QDate date; // fecha local
    int minutes = 0;
};

// Agregados de sesiones de estudio (solo sesiones con trabajo registrado).
class StatsService {
public:
    explicit StatsService(QSqlDatabase db);

    QList<SubjectHours> hoursBySubject() const;
    // Serie continua de los últimos `days` días (incluye días a cero).
    QList<DailyMinutes> minutesPerDay(int days) const;
    int completedSessionCount() const;
    qint64 totalWorkSeconds() const;

private:
    QSqlDatabase m_db;
};

} // namespace pass
