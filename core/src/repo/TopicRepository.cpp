// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/TopicRepository.h"

#include "pass/sync/DataChangeNotifier.h"
#include "pass/sync/Tombstones.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTimeZone>

namespace pass {

namespace {

QString nowIsoUtc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

Topic fromRow(const QSqlQuery& q) {
    Topic t;
    t.id = QUuid::fromString(q.value(0).toString());
    t.subjectId = QUuid::fromString(q.value(1).toString());
    t.name = q.value(2).toString();
    t.updatedAt = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
    t.updatedAt.setTimeZone(QTimeZone::utc());
    return t;
}

constexpr auto kColumns = "id, subject_id, name, updated_at";

} // namespace

TopicRepository::TopicRepository(QSqlDatabase db) : m_db(std::move(db)) {}

QList<Topic> TopicRepository::bySubject(const QUuid& subjectId) const {
    QList<Topic> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM topics WHERE subject_id = ? ORDER BY name COLLATE NOCASE")
                  .arg(QLatin1String(kColumns)));
    q.addBindValue(subjectId.toString(QUuid::WithoutBraces));
    if (!q.exec())
        return result;
    while (q.next())
        result.append(fromRow(q));
    return result;
}

std::optional<Topic> TopicRepository::byId(const QUuid& id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM topics WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

std::optional<Topic> TopicRepository::bySubjectAndName(const QUuid& subjectId,
                                                       const QString& name) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM topics WHERE subject_id = ? AND name = ?")
                  .arg(QLatin1String(kColumns)));
    q.addBindValue(subjectId.toString(QUuid::WithoutBraces));
    q.addBindValue(name);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool TopicRepository::add(const Topic& topic) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO topics(id, subject_id, name, updated_at) VALUES(?, ?, ?, ?)"));
    q.addBindValue(topic.id.toString(QUuid::WithoutBraces));
    q.addBindValue(topic.subjectId.toString(QUuid::WithoutBraces));
    q.addBindValue(topic.name);
    q.addBindValue(nowIsoUtc());
    if (!q.exec())
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool TopicRepository::update(const Topic& topic) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE topics SET subject_id = ?, name = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(topic.subjectId.toString(QUuid::WithoutBraces));
    q.addBindValue(topic.name);
    q.addBindValue(nowIsoUtc());
    q.addBindValue(topic.id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1)
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool TopicRepository::remove(const QUuid& id) {
    // Borrado y tombstone en la misma transacción (igual que SubjectRepository):
    // el borrado debe propagarse sin ambigüedad a los demás dispositivos.
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM topics WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1 || !insertTombstone(m_db, entity::kTopics, id)) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit())
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

} // namespace pass
