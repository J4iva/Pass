// SPDX-License-Identifier: GPL-3.0-or-later
#include "NewNoteDialog.h"

#include "../util/SubjectUtil.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

NewNoteDialog::NewNoteDialog(pass::SubjectRepository* subjects, QWidget* parent)
    : QDialog(parent), m_subjects(subjects), m_subject(new QComboBox),
      m_topic(new QLineEdit) {
    setWindowTitle(tr("Nueva nota"));
    setMinimumWidth(380);

    m_subject->setEditable(true);
    m_subject->addItem(QString());
    if (m_subjects) {
        for (const auto& s : m_subjects->all())
            m_subject->addItem(s.name);
    }
    m_topic->setPlaceholderText(tr("integrales, tema 4, ideas..."));

    auto* form = new QFormLayout;
    form->addRow(tr("Asignatura"), m_subject);
    form->addRow(tr("Tema"), m_topic);

    auto* hint = new QLabel(tr("Rellena al menos uno. Con asignatura se crea una "
                               "plantilla de estudio; sin ella, una nota libre."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
}

QString NewNoteDialog::subject() const {
    return m_subject->currentText().trimmed();
}

QString NewNoteDialog::topic() const {
    return m_topic->text().trimmed();
}

void NewNoteDialog::accept() {
    if (subject().isEmpty() && topic().isEmpty()) {
        QMessageBox::warning(this, tr("Nueva nota"),
                             tr("Indica al menos la asignatura o el tema."));
        return;
    }
    // Mantiene el listado de asignaturas consistente con el resto de la app.
    if (m_subjects && !subject().isEmpty())
        util::ensureSubject(*m_subjects, subject());
    QDialog::accept();
}
