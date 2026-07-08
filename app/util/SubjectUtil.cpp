// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubjectUtil.h"

namespace util {

QString colorForName(const QString& name) {
    static const QStringList palette = {
        QStringLiteral("#e57373"), QStringLiteral("#64b5f6"), QStringLiteral("#81c784"),
        QStringLiteral("#ffb74d"), QStringLiteral("#ba68c8"), QStringLiteral("#4db6ac"),
        QStringLiteral("#f06292"), QStringLiteral("#a1887f")};
    return palette[qAbs(qHash(name)) % palette.size()];
}

QUuid ensureSubject(pass::SubjectRepository& repo, const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};
    for (const auto& s : repo.all(/*includeArchived=*/true)) {
        if (s.name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return s.id;
    }
    pass::Subject created{QUuid::createUuid(), trimmed, colorForName(trimmed), false};
    if (!repo.add(created))
        return {};
    return created.id;
}

QUuid ensureTopic(pass::TopicRepository& repo, const QUuid& subjectId, const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || subjectId.isNull())
        return {};
    for (const auto& t : repo.bySubject(subjectId)) {
        if (t.name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return t.id;
    }
    pass::Topic created{QUuid::createUuid(), subjectId, trimmed, {}};
    if (!repo.add(created))
        return {};
    return created.id;
}

} // namespace util
