// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/SubjectRepository.h"

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

Subject fromRow(const QSqlQuery& q) {
    Subject s;
    s.id = QUuid::fromString(q.value(0).toString());
    s.name = q.value(1).toString();
    s.colorHex = q.value(2).toString();
    s.archived = q.value(3).toBool();
    s.updatedAt = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
    s.updatedAt.setTimeZone(QTimeZone::utc());
    return s;
}

constexpr auto kColumns = "id, name, color, archived, updated_at";

} // namespace

SubjectRepository::SubjectRepository(QSqlDatabase db) : m_db(std::move(db)) {}

QList<Subject> SubjectRepository::all(bool includeArchived) const {
    QList<Subject> result;
    QSqlQuery q(m_db);
    const QString sql = QStringLiteral("SELECT %1 FROM subjects %2 ORDER BY name COLLATE NOCASE")
                            .arg(QLatin1String(kColumns),
                                 includeArchived ? QString() : QStringLiteral("WHERE archived = 0"));
    if (!q.exec(sql))
        return result;
    while (q.next())
        result.append(fromRow(q));
    return result;
}

std::optional<Subject> SubjectRepository::byId(const QUuid& id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM subjects WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

std::optional<Subject> SubjectRepository::byName(const QString& name) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM subjects WHERE name = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(name);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool SubjectRepository::add(const Subject& subject) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO subjects(id, name, color, archived, updated_at) VALUES(?, ?, ?, ?, ?)"));
    q.addBindValue(subject.id.toString(QUuid::WithoutBraces));
    q.addBindValue(subject.name);
    q.addBindValue(subject.colorHex);
    q.addBindValue(subject.archived ? 1 : 0);
    q.addBindValue(nowIsoUtc());
    if (!q.exec())
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool SubjectRepository::update(const Subject& subject) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE subjects SET name = ?, color = ?, archived = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(subject.name);
    q.addBindValue(subject.colorHex);
    q.addBindValue(subject.archived ? 1 : 0);
    q.addBindValue(nowIsoUtc());
    q.addBindValue(subject.id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1)
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool SubjectRepository::remove(const QUuid& id) {
    // Borrado y tombstone en la misma transacción: el borrado debe propagarse a
    // los demás dispositivos sin ambigüedad (nunca "archivo ausente = borrado").
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM subjects WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1 || !insertTombstone(m_db, entity::kSubjects, id)) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit())
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

} // namespace pass
