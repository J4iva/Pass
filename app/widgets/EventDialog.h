// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"
#include "pass/repo/SubjectRepository.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QPlainTextEdit;
class QTimeEdit;

// Alta/edición de un evento de calendario. Trabaja en hora local y convierte
// a UTC solo al construir el CalendarEvent resultante.
class EventDialog : public QDialog {
    Q_OBJECT

public:
    EventDialog(pass::SubjectRepository& subjects, const QDate& defaultDate,
                QWidget* parent = nullptr);

    void loadEvent(const pass::CalendarEvent& event);
    // Devuelve el evento con los campos del formulario (conserva id y metadatos
    // del evento cargado, si lo hubo).
    pass::CalendarEvent result() const;

    void accept() override;

private:
    pass::CalendarEvent m_base;
    QList<pass::Subject> m_subjectList;

    QLineEdit* m_title;
    QCheckBox* m_allDay;
    QDateEdit* m_date;
    QTimeEdit* m_start;
    QTimeEdit* m_end;
    QComboBox* m_subject;
    QPlainTextEdit* m_description;
};
