// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionTableModel.h"

using namespace pass;

SessionTableModel::SessionTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void SessionTableModel::reload(QList<StudySession> sessions, QHash<QUuid, QString> subjectNames) {
    beginResetModel();
    m_sessions = std::move(sessions);
    m_subjectNames = std::move(subjectNames);
    endResetModel();
}

int SessionTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_sessions.size());
}

int SessionTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SessionTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_sessions.size())
        return {};
    const StudySession& s = m_sessions[index.row()];

    if (role == Qt::TextAlignmentRole && index.column() == Minutes)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case Date:
        return s.startedAt.isValid()
                   ? s.startedAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"))
                   : QString();
    case Subject:
        return m_subjectNames.value(s.subjectId, tr("(sin asignatura)"));
    case Topic:
        return s.topic;
    case Status:
        switch (s.status) {
        case SessionStatus::Completed:
            return tr("Completada");
        case SessionStatus::Aborted:
            return tr("Interrumpida");
        case SessionStatus::Planned:
            return tr("Planificada");
        }
        return {};
    case Minutes:
        return s.actualSeconds / 60;
    }
    return {};
}

QVariant SessionTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Date:
        return tr("Fecha");
    case Subject:
        return tr("Asignatura");
    case Topic:
        return tr("Tema");
    case Status:
        return tr("Estado");
    case Minutes:
        return tr("Minutos");
    }
    return {};
}
