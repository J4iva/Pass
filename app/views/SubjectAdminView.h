// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QWidget>

class QListWidget;
class QPushButton;
class QLabel;

namespace pass {
class AppSettings;
class CalendarService;
} // namespace pass

// Administración de asignaturas y sus temas:
//   - asignaturas: crear, renombrar (propaga a eventos/tareas/notas) y eliminar
//     (borra tareas y notas, conserva el histórico de sesiones sin asignatura).
//   - temas: entidad propia por asignatura (crear/renombrar/eliminar).
// La lógica de cascada vive en pass::SubjectAdminService (core); aquí solo va la UI.
class SubjectAdminView : public QWidget {
    Q_OBJECT

public:
    SubjectAdminView(QSqlDatabase db, pass::AppSettings* settings,
                     pass::CalendarService* calendar = nullptr, QWidget* parent = nullptr);

private:
    void refreshSubjects();
    void refreshTopics();
    QUuid selectedSubject() const;
    QUuid selectedTopic() const;

    void onNewSubject();
    void onRenameSubject();
    void onDeleteSubject();
    void onNewTopic();
    void onRenameTopic();
    void onDeleteTopic();

    QSqlDatabase m_db;
    pass::AppSettings* m_settings;
    pass::CalendarService* m_calendar;

    QListWidget* m_subjects;
    QListWidget* m_topics;
    QLabel* m_topicsTitle;
    QPushButton* m_renameSubject;
    QPushButton* m_deleteSubject;
    QPushButton* m_newTopic;
    QPushButton* m_renameTopic;
    QPushButton* m_deleteTopic;
};
