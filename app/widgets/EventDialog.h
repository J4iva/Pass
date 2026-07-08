// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimeEdit;

// Alta/edición de un evento de calendario. Trabaja en hora local y convierte
// a UTC solo al construir el CalendarEvent resultante.
class EventDialog : public QDialog {
    Q_OBJECT

public:
    // `offerGoogleUpload` muestra el check "crear también en Google Calendar"
    // (solo para eventos nuevos y con la cuenta de Google conectada).
    EventDialog(pass::SubjectRepository& subjects, pass::TopicRepository& topics,
                const QDate& defaultDate, bool offerGoogleUpload = false,
                QWidget* parent = nullptr);

    // Modo tarea: el título se guarda con el prefijo "[T]" (visible también en
    // Google Calendar) y la asignatura pasa a ser obligatoria. loadEvent lo
    // activa solo si el evento cargado ya es una tarea.
    void setTaskMode(bool task);

    void loadEvent(const pass::CalendarEvent& event);
    // Devuelve el evento con los campos del formulario (conserva id y metadatos
    // del evento cargado, si lo hubo).
    pass::CalendarEvent result() const;

    void accept() override;

private:
    // Muestra bajo las notas, como ayuda, los temas ya creados de la asignatura
    // seleccionada (no crea entidades; el tema se escribe libre en las notas).
    void updateTopicHint();

    pass::CalendarEvent m_base;
    QList<pass::Subject> m_subjectList;
    pass::TopicRepository& m_topics;
    bool m_taskMode = false;

    QLineEdit* m_title;
    QCheckBox* m_allDay;
    QCheckBox* m_googleUpload = nullptr; // nulo si no se ofrece subir a Google
    QDateEdit* m_date;
    QTimeEdit* m_start;
    QTimeEdit* m_end;
    QComboBox* m_subject;
    QPlainTextEdit* m_description;
    QLabel* m_topicHint;
};
