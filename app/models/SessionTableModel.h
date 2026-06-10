// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/StudySession.h"

#include <QAbstractTableModel>
#include <QHash>

class SessionTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Date = 0, Subject, Topic, Status, Minutes, ColumnCount };

    explicit SessionTableModel(QObject* parent = nullptr);

    void reload(QList<pass::StudySession> sessions, QHash<QUuid, QString> subjectNames);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<pass::StudySession> m_sessions;
    QHash<QUuid, QString> m_subjectNames;
};
