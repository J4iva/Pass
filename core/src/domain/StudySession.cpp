// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/domain/StudySession.h"

namespace pass {

QString sessionStatusToString(SessionStatus status) {
    switch (status) {
    case SessionStatus::Planned:
        return QStringLiteral("planned");
    case SessionStatus::Completed:
        return QStringLiteral("completed");
    case SessionStatus::Aborted:
        return QStringLiteral("aborted");
    }
    return QStringLiteral("planned");
}

SessionStatus sessionStatusFromString(const QString& text) {
    if (text == QLatin1String("completed"))
        return SessionStatus::Completed;
    if (text == QLatin1String("aborted"))
        return SessionStatus::Aborted;
    return SessionStatus::Planned;
}

} // namespace pass
