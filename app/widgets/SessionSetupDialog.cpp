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
#include <QSpinBox>
#include <QVBoxLayout>

using namespace pass;

SessionSetupDialog::SessionSetupDialog(SubjectRepository& subjects,
                                       StrategyRepository& strategies, QWidget* parent)
    : QDialog(parent), m_subjects(subjects), m_strategies(strategies),
      m_minutes(new QSpinBox), m_subject(new QComboBox), m_topic(new QLineEdit),
      m_proposals(new QListWidget), m_planLater(new QCheckBox(tr("Planificar para más tarde"))),
      m_plannedStart(new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600))) {
    setWindowTitle(tr("Nueva sesión de estudio"));
    setMinimumWidth(420);

    m_minutes->setRange(15, 480);
    m_minutes->setValue(120);
    m_minutes->setSuffix(tr(" min"));
    m_minutes->setSingleStep(15);

    m_subject->setEditable(true);
    m_subject->addItem(QString()); // sin asignatura
    for (const auto& s : m_subjects.all())
        m_subject->addItem(s.name);

    m_topic->setPlaceholderText(tr("Tema (opcional): integrales, tema 4..."));

    auto* form = new QFormLayout;
    form->addRow(tr("¿Cuánto quieres estudiar?"), m_minutes);
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

void SessionSetupDialog::refreshProposals() {
    m_plans = StrategyCatalog::proposals(m_minutes->value(), m_strategies.all());
    m_proposals->clear();
    for (const auto& plan : m_plans) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(plan.strategy.name, StrategyCatalog::describe(plan)));
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
    return m_topic->text().trimmed();
}

std::optional<QDateTime> SessionSetupDialog::plannedStart() const {
    if (!m_planLater->isChecked())
        return std::nullopt;
    return m_plannedStart->dateTime();
}
