// SPDX-License-Identifier: GPL-3.0-or-later
#include "NewNoteDialog.h"

#include "../util/SubjectUtil.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

NewNoteDialog::NewNoteDialog(pass::SubjectRepository* subjects, pass::TopicRepository* topics,
                             QWidget* parent)
    : QDialog(parent), m_subjects(subjects), m_topics(topics), m_subject(new QComboBox),
      m_topic(new QComboBox) {
    setWindowTitle(tr("Nueva nota"));
    setMinimumWidth(380);

    m_subject->setEditable(true);
    m_subject->addItem(QString());
    if (m_subjects) {
        for (const auto& s : m_subjects->all())
            m_subject->addItem(s.name);
    }
    m_topic->setEditable(true);
    m_topic->setInsertPolicy(QComboBox::NoInsert);
    if (auto* edit = m_topic->lineEdit())
        edit->setPlaceholderText(tr("integrales, tema 4, ideas..."));
    reloadTopics();
    // Al cambiar la asignatura, mostrar sus temas existentes.
    connect(m_subject, &QComboBox::currentTextChanged, this, &NewNoteDialog::reloadTopics);

    auto* form = new QFormLayout;
    form->addRow(tr("Asignatura"), m_subject);
    form->addRow(tr("Tema"), m_topic);

    auto* hint = new QLabel(tr("Rellena al menos uno. Con asignatura se crea una "
                               "plantilla de trabajo; sin ella, una nota libre."));
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
    return m_topic->currentText().trimmed();
}

void NewNoteDialog::reloadTopics() {
    // Conserva lo que el usuario hubiera escrito mientras se repuebla la lista.
    const QString current = m_topic->currentText();
    QSignalBlocker blocker(m_topic);
    m_topic->clear();
    m_topic->addItem(QString());
    const QString name = subject();
    if (m_subjects && m_topics && !name.isEmpty()) {
        for (const auto& s : m_subjects->all(/*includeArchived=*/true)) {
            if (s.name.compare(name, Qt::CaseInsensitive) == 0) {
                for (const auto& t : m_topics->bySubject(s.id))
                    m_topic->addItem(t.name);
                break;
            }
        }
    }
    m_topic->setCurrentText(current);
}

void NewNoteDialog::accept() {
    if (subject().isEmpty() && topic().isEmpty()) {
        QMessageBox::warning(this, tr("Nueva nota"),
                             tr("Indica al menos la asignatura o el tema."));
        return;
    }
    // Mantiene asignaturas y temas consistentes con el resto de la app: crea
    // los que el usuario haya escrito y no existieran todavía.
    if (m_subjects && !subject().isEmpty()) {
        const QUuid subjectId = util::ensureSubject(*m_subjects, subject());
        if (m_topics && !subjectId.isNull() && !topic().isEmpty())
            util::ensureTopic(*m_topics, subjectId, topic());
    }
    QDialog::accept();
}
