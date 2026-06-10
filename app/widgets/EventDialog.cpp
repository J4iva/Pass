// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTimeEdit>
#include <QVBoxLayout>

using namespace pass;

EventDialog::EventDialog(SubjectRepository& subjects, const QDate& defaultDate, QWidget* parent)
    : QDialog(parent), m_title(new QLineEdit), m_allDay(new QCheckBox(tr("Todo el día"))),
      m_date(new QDateEdit(defaultDate)), m_start(new QTimeEdit(QTime(9, 0))),
      m_end(new QTimeEdit(QTime(10, 0))), m_subject(new QComboBox),
      m_description(new QPlainTextEdit) {
    setWindowTitle(tr("Evento"));
    setMinimumWidth(400);

    m_date->setCalendarPopup(true);
    m_description->setMaximumHeight(80);

    m_subjectList = subjects.all();
    m_subject->addItem(tr("(ninguna)"));
    for (const auto& s : m_subjectList)
        m_subject->addItem(s.name);

    auto* form = new QFormLayout;
    form->addRow(tr("Título"), m_title);
    form->addRow(QString(), m_allDay);
    form->addRow(tr("Fecha"), m_date);
    form->addRow(tr("Inicio"), m_start);
    form->addRow(tr("Fin"), m_end);
    form->addRow(tr("Asignatura"), m_subject);
    form->addRow(tr("Notas"), m_description);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_allDay, &QCheckBox::toggled, this, [this](bool allDay) {
        m_start->setEnabled(!allDay);
        m_end->setEnabled(!allDay);
    });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void EventDialog::loadEvent(const CalendarEvent& event) {
    m_base = event;
    m_title->setText(event.title);
    m_allDay->setChecked(event.allDay);
    const QDateTime startLocal = event.startUtc.toLocalTime();
    m_date->setDate(startLocal.date());
    m_start->setTime(startLocal.time());
    m_end->setTime(event.endUtc.toLocalTime().time());
    m_description->setPlainText(event.description);
    for (int i = 0; i < m_subjectList.size(); ++i) {
        if (m_subjectList[i].id == event.subjectId) {
            m_subject->setCurrentIndex(i + 1); // +1 por "(ninguna)"
            break;
        }
    }
}

CalendarEvent EventDialog::result() const {
    CalendarEvent e = m_base;
    e.title = m_title->text().trimmed();
    e.description = m_description->toPlainText().trimmed();
    e.allDay = m_allDay->isChecked();

    const QDate date = m_date->date();
    if (e.allDay) {
        e.startUtc = QDateTime(date, QTime(0, 0)).toUTC();
        e.endUtc = QDateTime(date.addDays(1), QTime(0, 0)).toUTC();
    } else {
        e.startUtc = QDateTime(date, m_start->time()).toUTC();
        e.endUtc = QDateTime(date, m_end->time()).toUTC();
    }

    const int idx = m_subject->currentIndex() - 1;
    e.subjectId = (idx >= 0 && idx < m_subjectList.size()) ? m_subjectList[idx].id : QUuid();
    return e;
}

void EventDialog::accept() {
    if (m_title->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Evento"), tr("El título no puede estar vacío."));
        return;
    }
    if (!m_allDay->isChecked() && m_end->time() <= m_start->time()) {
        QMessageBox::warning(this, tr("Evento"), tr("La hora de fin debe ser posterior a la de inicio."));
        return;
    }
    QDialog::accept();
}
