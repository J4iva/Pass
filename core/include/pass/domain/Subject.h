// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace pass {

struct Subject {
    QUuid id;
    QString name;
    QString colorHex; // "#RRGGBB", vacío = color por defecto
    bool archived = false;
    QDateTime updatedAt; // UTC; marca de última escritura (sync entre dispositivos)
};

} // namespace pass
