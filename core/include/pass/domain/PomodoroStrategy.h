// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace pass {

struct PomodoroStrategy {
    QUuid id;
    QString name;
    int workMinutes = 25;
    int breakMinutes = 5;
    int longBreakMinutes = 15;
    int cyclesBeforeLongBreak = 4;
    bool builtin = false;
    QDateTime updatedAt; // UTC; marca de última escritura (sync entre dispositivos)
};

} // namespace pass
