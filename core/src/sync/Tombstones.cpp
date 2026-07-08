// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/Tombstones.h"

#include <QDateTime>
#include <QSqlQuery>

namespace pass {

bool insertTombstone(QSqlDatabase& db, const char* entity, const QUuid& id) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO tombstones(entity, id, deleted_at) VALUES(?, ?, ?) "
        "ON CONFLICT(entity, id) DO UPDATE SET deleted_at = excluded.deleted_at"));
    q.addBindValue(QString::fromLatin1(entity));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return q.exec();
}

} // namespace pass
