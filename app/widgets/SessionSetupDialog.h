// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"
#include "pass/session/StrategyCatalog.h"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLineEdit;
class QListWidget;
class QSpinBox;

// Pide duración objetivo, asignatura y tema, y propone los planes del
// StrategyCatalog que encajan en ese tiempo. Opcionalmente la sesión se puede
// asignar a una tarea pendiente (`tasks`): al elegirla se rellenan asignatura
// y tema y la sesión queda vinculada (sessions.event_id) para sumar sus horas.
class SessionSetupDialog : public QDialog {
    Q_OBJECT

public:
    SessionSetupDialog(pass::SubjectRepository& subjects, pass::TopicRepository& topics,
                       pass::StrategyRepository& strategies,
                       const QList<pass::CalendarEvent>& tasks = {}, QWidget* parent = nullptr);

    // Restringe el diálogo a planificar (sin opción de empezar ya): oculta el
    // check "Planificar para más tarde" y fija la fecha de inicio a `date`.
    void setPlanOnly(const QDate& date);

    std::optional<pass::SessionPlan> selectedPlan() const;
    // Resuelve la asignatura escrita: la busca por nombre o la crea. Nula si vacío.
    QUuid resolveSubjectId();
    QString topic() const;
    // Con valor si el usuario eligió planificar la sesión en vez de empezarla ya.
    std::optional<QDateTime> plannedStart() const;
    // Tarea a la que se asigna la sesión; nula si "(ninguna)".
    QUuid selectedTaskId() const;

    // Crea la asignatura y el tema escritos si no existían todavía.
    void accept() override;

private:
    void refreshProposals();
    // Recarga la lista de temas del combo según la asignatura seleccionada.
    void reloadTopics();

    pass::SubjectRepository& m_subjects;
    pass::TopicRepository& m_topics;
    pass::StrategyRepository& m_strategies;
    QList<pass::SessionPlan> m_plans;
    QList<pass::CalendarEvent> m_tasks;

    QSpinBox* m_minutes;
    QComboBox* m_task = nullptr; // solo existe si hay tareas pendientes
    QComboBox* m_subject;
    QComboBox* m_topic;
    QListWidget* m_proposals;
    QCheckBox* m_planLater;
    QDateTimeEdit* m_plannedStart;
};
