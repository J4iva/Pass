// SPDX-License-Identifier: GPL-3.0-or-later
#include "EventDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTimeEdit>
#include <QVBoxLayout>

using namespace pass;

EventDialog::EventDialog(SubjectRepository& subjects, TopicRepository& topics,
                         const QDate& defaultDate, bool offerGoogleUpload, QWidget* parent)
    : QDialog(parent), m_topics(topics), m_title(new QLineEdit),
      m_allDay(new QCheckBox(tr("Todo el día"))), m_date(new QDateEdit(defaultDate)),
      m_start(new QTimeEdit(QTime(9, 0))), m_end(new QTimeEdit(QTime(10, 0))),
      m_subject(new QComboBox), m_description(new QPlainTextEdit), m_topicHint(new QLabel) {
    setWindowTitle(tr("Evento"));
    setMinimumWidth(400);

    m_date->setCalendarPopup(true);
    m_description->setMaximumHeight(80);

    m_subjectList = subjects.all();
    m_subject->addItem(tr("(ninguna)"));
    for (const auto& s : m_subjectList)
        m_subject->addItem(s.name);

    m_topicHint->setWordWrap(true);
    m_topicHint->setObjectName("hint");
    m_topicHint->hide();

    auto* form = new QFormLayout;
    form->addRow(tr("Título"), m_title);
    form->addRow(QString(), m_allDay);
    form->addRow(tr("Fecha"), m_date);
    form->addRow(tr("Inicio"), m_start);
    form->addRow(tr("Fin"), m_end);
    form->addRow(tr("Asignatura"), m_subject);
    form->addRow(tr("Notas"), m_description);
    form->addRow(QString(), m_topicHint);

    // Al cambiar la asignatura, refrescar la lista de temas sugeridos.
    connect(m_subject, &QComboBox::currentIndexChanged, this, &EventDialog::updateTopicHint);
    updateTopicHint();

    if (offerGoogleUpload) {
        m_googleUpload = new QCheckBox(tr("Crear también en Google Calendar"));
        m_googleUpload->setChecked(true); // marcado por defecto
        form->addRow(QString(), m_googleUpload);
    }

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

void EventDialog::setTaskMode(bool task) {
    m_taskMode = task;
    setWindowTitle(task ? tr("Tarea") : tr("Evento"));
    if (task) {
        m_title->setPlaceholderText(tr("Entrega práctica 2, examen parcial..."));
        m_description->setPlaceholderText(tr("Tema o temas: tema 3, integrales..."));
    } else {
        m_title->setPlaceholderText(QString());
        m_description->setPlaceholderText(QString());
    }
}

void EventDialog::loadEvent(const CalendarEvent& event) {
    m_base = event;
    // Si lo que se edita ya es una tarea, se entra en modo tarea y el título se
    // muestra sin el prefijo "[T]" (se vuelve a añadir al guardar).
    setTaskMode(isTask(event));
    m_title->setText(taskDisplayTitle(event));
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
    if (m_taskMode)
        e.title = kTaskTitlePrefix + QLatin1Char(' ') + e.title;
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

    // Si el usuario marcó "crear también en Google", la fila pasa a provider
    // 'google' y CalendarService la subirá (write-through). Nada se sube
    // retroactivamente: solo aplica al alta de un evento nuevo.
    if (m_googleUpload && m_googleUpload->isChecked())
        e.provider = QStringLiteral("google");
    return e;
}

void EventDialog::accept() {
    if (m_title->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("El título no puede estar vacío."));
        return;
    }
    if (!m_allDay->isChecked() && m_end->time() <= m_start->time()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("La hora de fin debe ser posterior a la de inicio."));
        return;
    }
    if (m_taskMode && m_subject->currentIndex() <= 0) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Una tarea necesita una asignatura asociada."));
        return;
    }
    QDialog::accept();
}

void EventDialog::updateTopicHint() {
    const int idx = m_subject->currentIndex() - 1; // -1 por "(ninguna)"
    QStringList names;
    if (idx >= 0 && idx < m_subjectList.size()) {
        for (const auto& t : m_topics.bySubject(m_subjectList[idx].id))
            names << t.name;
    }
    if (names.isEmpty()) {
        m_topicHint->clear();
        m_topicHint->hide();
    } else {
        m_topicHint->setText(tr("Temas de la asignatura: %1").arg(names.join(QStringLiteral(", "))));
        m_topicHint->show();
    }
}
