// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/SubjectRepository.h"

#include <QSqlQuery>

namespace pass {

namespace {

Subject fromRow(const QSqlQuery& q) {
    Subject s;
    s.id = QUuid::fromString(q.value(0).toString());
    s.name = q.value(1).toString();
    s.colorHex = q.value(2).toString();
    s.archived = q.value(3).toBool();
    return s;
}

} // namespace

SubjectRepository::SubjectRepository(QSqlDatabase db) : m_db(std::move(db)) {}

QList<Subject> SubjectRepository::all(bool includeArchived) const {
    QList<Subject> result;
    QSqlQuery q(m_db);
    const QString sql = QStringLiteral("SELECT id, name, color, archived FROM subjects %1 "
                                       "ORDER BY name COLLATE NOCASE")
                            .arg(includeArchived ? QString() : QStringLiteral("WHERE archived = 0"));
    if (!q.exec(sql))
        return result;
    while (q.next())
        result.append(fromRow(q));
    return result;
}

std::optional<Subject> SubjectRepository::byId(const QUuid& id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name, color, archived FROM subjects WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool SubjectRepository::add(const Subject& subject) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO subjects(id, name, color, archived) VALUES(?, ?, ?, ?)"));
    q.addBindValue(subject.id.toString(QUuid::WithoutBraces));
    q.addBindValue(subject.name);
    q.addBindValue(subject.colorHex);
    q.addBindValue(subject.archived ? 1 : 0);
    return q.exec();
}

bool SubjectRepository::update(const Subject& subject) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE subjects SET name = ?, color = ?, archived = ? WHERE id = ?"));
    q.addBindValue(subject.name);
    q.addBindValue(subject.colorHex);
    q.addBindValue(subject.archived ? 1 : 0);
    q.addBindValue(subject.id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

bool SubjectRepository::remove(const QUuid& id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM subjects WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

} // namespace pass
