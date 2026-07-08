// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/SyncStateRepository.h"

#include <QSqlQuery>

namespace pass {

SyncStateRepository::SyncStateRepository(QSqlDatabase db) : m_db(std::move(db)) {}

std::optional<QString> SyncStateRepository::get(const QString& key) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM sync_state WHERE key = ?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return q.value(0).toString();
}

bool SyncStateRepository::set(const QString& key, const QString& value) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO sync_state(key, value) VALUES(?, ?) "
                             "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

bool SyncStateRepository::remove(const QString& key) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sync_state WHERE key = ?"));
    q.addBindValue(key);
    return q.exec();
}

} // namespace pass
