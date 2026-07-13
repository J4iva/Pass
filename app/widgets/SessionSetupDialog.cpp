// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionSetupDialog.h"

#include "../util/SubjectUtil.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace pass;

SessionSetupDialog::SessionSetupDialog(SubjectRepository& subjects, TopicRepository& topics,
                                       StrategyRepository& strategies,
                                       const QList<CalendarEvent>& tasks, QWidget* parent)
    : QDialog(parent), m_subjects(subjects), m_topics(topics), m_strategies(strategies),
      m_tasks(tasks), m_minutes(new QSpinBox), m_subject(new QComboBox), m_topic(new QComboBox),
      m_proposals(new QListWidget), m_planLater(new QCheckBox(tr("Planificar para más tarde"))),
      m_plannedStart(new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600))) {
    setWindowTitle(tr("Nueva sesión de trabajo"));
    setMinimumWidth(420);

    m_minutes->setRange(15, 480);
    m_minutes->setValue(120);
    m_minutes->setSuffix(tr(" min"));
    m_minutes->setSingleStep(15);

    m_subject->setEditable(true);
    m_subject->addItem(QString()); // sin asignatura
    for (const auto& s : m_subjects.all())
        m_subject->addItem(s.name);

    m_topic->setEditable(true);
    m_topic->setInsertPolicy(QComboBox::NoInsert);
    if (auto* edit = m_topic->lineEdit())
        edit->setPlaceholderText(tr("Tema (opcional): integrales, tema 4..."));
    reloadTopics();
    // Al cambiar la asignatura (también al elegir una tarea), mostrar sus temas.
    connect(m_subject, &QComboBox::currentTextChanged, this, &SessionSetupDialog::reloadTopics);

    auto* form = new QFormLayout;
    form->addRow(tr("¿Cuánto quieres trabajar?"), m_minutes);
    if (!m_tasks.isEmpty()) {
        m_task = new QComboBox;
        m_task->addItem(tr("(ninguna)"));
        for (const auto& t : m_tasks)
            m_task->addItem(QStringLiteral("[T] %1 (%2)")
                                .arg(taskDisplayTitle(t),
                                     t.startUtc.toLocalTime().toString(QStringLiteral("dd/MM"))));
        // Elegir una tarea precarga su asignatura (las horas de la sesión se
        // sumarán a esa tarea en el dashboard).
        connect(m_task, &QComboBox::currentIndexChanged, this, [this](int idx) {
            const int i = idx - 1;
            if (i < 0 || i >= m_tasks.size())
                return;
            if (const auto subject = m_subjects.byId(m_tasks[i].subjectId))
                m_subject->setCurrentText(subject->name);
        });
        form->addRow(tr("Tarea"), m_task);
    }
    form->addRow(tr("Asignatura"), m_subject);
    form->addRow(tr("Tema"), m_topic);

    m_plannedStart->setCalendarPopup(true);
    m_plannedStart->setDisplayFormat(QStringLiteral("dd/MM/yyyy HH:mm"));
    m_plannedStart->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Empezar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_planLater, &QCheckBox::toggled, this, [this, buttons](bool later) {
        m_plannedStart->setEnabled(later);
        buttons->button(QDialogButtonBox::Ok)
            ->setText(later ? tr("Añadir al calendario") : tr("Empezar"));
    });

    auto* planRow = new QHBoxLayout;
    planRow->addWidget(m_planLater);
    planRow->addWidget(m_plannedStart, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Estrategias propuestas:")));
    layout->addWidget(m_proposals);
    layout->addLayout(planRow);
    layout->addWidget(buttons);

    connect(m_minutes, &QSpinBox::valueChanged, this, &SessionSetupDialog::refreshProposals);
    connect(m_proposals, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    refreshProposals();
}

void SessionSetupDialog::setPlanOnly(const QDate& date) {
    setWindowTitle(tr("Planificar sesión"));
    // Marcar el check ajusta el texto del botón y habilita la fecha (vía toggled);
    // luego lo ocultamos para que no se pueda volver a "empezar ya".
    m_planLater->setChecked(true);
    m_planLater->hide();
    QDateTime when = m_plannedStart->dateTime();
    when.setDate(date);
    m_plannedStart->setDateTime(when);
}

void SessionSetupDialog::refreshProposals() {
    m_plans = StrategyCatalog::proposals(m_minutes->value(), m_strategies.all());
    m_proposals->clear();
    for (const auto& plan : m_plans) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 // %2").arg(plan.strategy.name, StrategyCatalog::describe(plan)));
        m_proposals->addItem(item);
    }
    if (m_proposals->count() > 0)
        m_proposals->setCurrentRow(0);
}

std::optional<SessionPlan> SessionSetupDialog::selectedPlan() const {
    const int row = m_proposals->currentRow();
    if (row < 0 || row >= m_plans.size())
        return std::nullopt;
    return m_plans[row];
}

QUuid SessionSetupDialog::resolveSubjectId() {
    return util::ensureSubject(m_subjects, m_subject->currentText());
}

QString SessionSetupDialog::topic() const {
    return m_topic->currentText().trimmed();
}

void SessionSetupDialog::reloadTopics() {
    // Conserva lo que el usuario hubiera escrito mientras se repuebla la lista.
    const QString current = m_topic->currentText();
    QSignalBlocker blocker(m_topic);
    m_topic->clear();
    m_topic->addItem(QString());
    const QString name = m_subject->currentText().trimmed();
    if (!name.isEmpty()) {
        for (const auto& s : m_subjects.all(/*includeArchived=*/true)) {
            if (s.name.compare(name, Qt::CaseInsensitive) == 0) {
                for (const auto& t : m_topics.bySubject(s.id))
                    m_topic->addItem(t.name);
                break;
            }
        }
    }
    m_topic->setCurrentText(current);
}

void SessionSetupDialog::accept() {
    // Mantiene asignaturas y temas consistentes con el resto de la app: crea los
    // que el usuario haya escrito y no existieran todavía.
    const QString subjectName = m_subject->currentText().trimmed();
    const QString topicName = topic();
    if (!subjectName.isEmpty() && !topicName.isEmpty()) {
        const QUuid subjectId = util::ensureSubject(m_subjects, subjectName);
        if (!subjectId.isNull())
            util::ensureTopic(m_topics, subjectId, topicName);
    }
    QDialog::accept();
}

std::optional<QDateTime> SessionSetupDialog::plannedStart() const {
    if (!m_planLater->isChecked())
        return std::nullopt;
    return m_plannedStart->dateTime();
}

QUuid SessionSetupDialog::selectedTaskId() const {
    if (!m_task)
        return {};
    const int i = m_task->currentIndex() - 1; // -1 por "(ninguna)"
    return (i >= 0 && i < m_tasks.size()) ? m_tasks[i].id : QUuid();
}
