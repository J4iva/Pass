// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/session/StrategyCatalog.h"

#include <QDialog>

#include <optional>

class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;

// Pide duración objetivo, asignatura y tema, y propone los planes del
// StrategyCatalog que encajan en ese tiempo.
class SessionSetupDialog : public QDialog {
    Q_OBJECT

public:
    SessionSetupDialog(pass::SubjectRepository& subjects, pass::StrategyRepository& strategies,
                       QWidget* parent = nullptr);

    std::optional<pass::SessionPlan> selectedPlan() const;
    // Resuelve la asignatura escrita: la busca por nombre o la crea. Nula si vacío.
    QUuid resolveSubjectId();
    QString topic() const;

private:
    void refreshProposals();

    pass::SubjectRepository& m_subjects;
    pass::StrategyRepository& m_strategies;
    QList<pass::SessionPlan> m_plans;

    QSpinBox* m_minutes;
    QComboBox* m_subject;
    QLineEdit* m_topic;
    QListWidget* m_proposals;
};
