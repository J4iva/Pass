// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubjectAdminView.h"

#include "pass/admin/SubjectAdminService.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"
#include "pass/settings/AppSettings.h"
#include "pass/sync/DataChangeNotifier.h"
#include "../theme/Theme.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace pass;

namespace {
constexpr int kIdRole = Qt::UserRole;
}

SubjectAdminView::SubjectAdminView(QSqlDatabase db, AppSettings* settings,
                                   CalendarService* calendar, QWidget* parent)
    : QWidget(parent), m_db(std::move(db)), m_settings(settings), m_calendar(calendar),
      m_subjects(new QListWidget), m_topics(new QListWidget),
      m_topicsTitle(new QLabel(tr("Temas"))) {
    // --- Columna de asignaturas ---
    auto* newSubject = new QPushButton(tr("Nueva"));
    m_renameSubject = new QPushButton(tr("Renombrar"));
    m_deleteSubject = new QPushButton(tr("Eliminar"));
    auto* subjectButtons = new QHBoxLayout;
    subjectButtons->addWidget(newSubject);
    subjectButtons->addWidget(m_renameSubject);
    subjectButtons->addWidget(m_deleteSubject);
    subjectButtons->addStretch();

    auto* subjectsCol = new QVBoxLayout;
    subjectsCol->setContentsMargins(12, 12, 6, 12);
    subjectsCol->addWidget(pass::theme::sectionLabel(tr("Asignaturas")));
    subjectsCol->addWidget(m_subjects, 1);
    subjectsCol->addLayout(subjectButtons);

    // --- Columna de temas ---
    m_newTopic = new QPushButton(tr("Nuevo tema"));
    m_renameTopic = new QPushButton(tr("Renombrar"));
    m_deleteTopic = new QPushButton(tr("Eliminar"));
    auto* topicButtons = new QHBoxLayout;
    topicButtons->addWidget(m_newTopic);
    topicButtons->addWidget(m_renameTopic);
    topicButtons->addWidget(m_deleteTopic);
    topicButtons->addStretch();

    auto* topicsCol = new QVBoxLayout;
    topicsCol->setContentsMargins(6, 12, 12, 12);
    m_topicsTitle->setObjectName("sectionLabel");
    topicsCol->addWidget(m_topicsTitle);
    topicsCol->addWidget(m_topics, 1);
    topicsCol->addLayout(topicButtons);

    auto* columns = new QHBoxLayout(this);
    columns->addLayout(subjectsCol, 1);
    columns->addLayout(topicsCol, 1);

    connect(newSubject, &QPushButton::clicked, this, &SubjectAdminView::onNewSubject);
    connect(m_renameSubject, &QPushButton::clicked, this, &SubjectAdminView::onRenameSubject);
    connect(m_deleteSubject, &QPushButton::clicked, this, &SubjectAdminView::onDeleteSubject);
    connect(m_newTopic, &QPushButton::clicked, this, &SubjectAdminView::onNewTopic);
    connect(m_renameTopic, &QPushButton::clicked, this, &SubjectAdminView::onRenameTopic);
    connect(m_deleteTopic, &QPushButton::clicked, this, &SubjectAdminView::onDeleteTopic);
    connect(&DataChangeNotifier::instance(), &DataChangeNotifier::changed,
            this, &SubjectAdminView::refreshSubjects);
    connect(m_subjects, &QListWidget::currentRowChanged, this,
            [this] { refreshTopics(); });
    connect(m_topics, &QListWidget::currentRowChanged, this, [this] {
        const bool hasTopic = !selectedTopic().isNull();
        m_renameTopic->setEnabled(hasTopic);
        m_deleteTopic->setEnabled(hasTopic);
    });

    refreshSubjects();
}

QUuid SubjectAdminView::selectedSubject() const {
    auto* item = m_subjects->currentItem();
    return item ? item->data(kIdRole).toUuid() : QUuid();
}

QUuid SubjectAdminView::selectedTopic() const {
    auto* item = m_topics->currentItem();
    return item ? item->data(kIdRole).toUuid() : QUuid();
}

void SubjectAdminView::refreshSubjects() {
    const QUuid keep = selectedSubject();
    m_subjects->clear();
    SubjectRepository subjects(m_db);
    for (const auto& s : subjects.all(true)) {
        auto* item = new QListWidgetItem(s.name, m_subjects);
        item->setData(kIdRole, s.id);
        if (s.id == keep)
            m_subjects->setCurrentItem(item);
    }
    if (!m_subjects->currentItem() && m_subjects->count() > 0)
        m_subjects->setCurrentRow(0);
    refreshTopics();
}

void SubjectAdminView::refreshTopics() {
    const QUuid subjectId = selectedSubject();
    const QUuid keep = selectedTopic();
    m_topics->clear();
    const bool hasSubject = !subjectId.isNull();
    if (hasSubject) {
        TopicRepository topics(m_db);
        for (const auto& t : topics.bySubject(subjectId)) {
            auto* item = new QListWidgetItem(t.name, m_topics);
            item->setData(kIdRole, t.id);
            if (t.id == keep)
                m_topics->setCurrentItem(item);
        }
    }
    auto* current = m_subjects->currentItem();
    m_topicsTitle->setText(hasSubject ? tr("Temas de «%1»").arg(current->text()) : tr("Temas"));

    m_renameSubject->setEnabled(hasSubject);
    m_deleteSubject->setEnabled(hasSubject);
    m_newTopic->setEnabled(hasSubject);
    const bool hasTopic = !selectedTopic().isNull();
    m_renameTopic->setEnabled(hasTopic);
    m_deleteTopic->setEnabled(hasTopic);
}

void SubjectAdminView::onNewSubject() {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Nueva asignatura"), tr("Nombre:"), QLineEdit::Normal,
                              QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    Subject s;
    s.id = QUuid::createUuid();
    s.name = name;
    SubjectRepository subjects(m_db);
    if (!subjects.add(s)) {
        QMessageBox::warning(this, tr("Nueva asignatura"),
                             tr("No se pudo crear. ¿Ya existe una asignatura con ese nombre?"));
        return;
    }
    refreshSubjects();
}

void SubjectAdminView::onRenameSubject() {
    const QUuid id = selectedSubject();
    if (id.isNull())
        return;
    auto* item = m_subjects->currentItem();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renombrar asignatura"), tr("Nuevo nombre:"),
                                               QLineEdit::Normal, item->text(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    SubjectAdminService admin(m_db, m_settings->vaultPath(), m_settings->vaultSubfolder(),
                              m_calendar);
    QString error;
    if (!admin.rename(id, name, error)) {
        QMessageBox::warning(this, tr("Renombrar asignatura"), error);
        return;
    }
    refreshSubjects();
}

void SubjectAdminView::onDeleteSubject() {
    const QUuid id = selectedSubject();
    if (id.isNull())
        return;
    auto* item = m_subjects->currentItem();
    SubjectAdminService admin(m_db, m_settings->vaultPath(), m_settings->vaultSubfolder(),
                              m_calendar);
    const auto impact = admin.impactOf(id);

    const QString detail =
        tr("Se eliminarán de forma permanente:\n"
           "• %1 tarea(s)\n"
           "• %2 nota(s)\n"
           "• %3 tema(s)\n\n"
           "Las %4 sesión(es) de estudio se conservarán, pero sin asignatura.")
            .arg(impact.tasks)
            .arg(impact.notes)
            .arg(impact.topics)
            .arg(impact.sessions);
    if (QMessageBox::question(this, tr("Eliminar «%1»").arg(item->text()),
                              tr("%1\n\n¿Continuar?").arg(detail)) != QMessageBox::Yes)
        return;

    QString error;
    if (!admin.remove(id, error)) {
        QMessageBox::warning(this, tr("Eliminar asignatura"), error);
        return;
    }
    refreshSubjects();
}

void SubjectAdminView::onNewTopic() {
    const QUuid subjectId = selectedSubject();
    if (subjectId.isNull())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nuevo tema"), tr("Nombre del tema:"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    Topic t;
    t.id = QUuid::createUuid();
    t.subjectId = subjectId;
    t.name = name;
    TopicRepository topics(m_db);
    if (!topics.add(t)) {
        QMessageBox::warning(this, tr("Nuevo tema"),
                             tr("No se pudo crear. ¿Ya existe ese tema en la asignatura?"));
        return;
    }
    refreshTopics();
}

void SubjectAdminView::onRenameTopic() {
    const QUuid id = selectedTopic();
    if (id.isNull())
        return;
    TopicRepository topics(m_db);
    const auto current = topics.byId(id);
    if (!current)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renombrar tema"), tr("Nuevo nombre:"),
                                               QLineEdit::Normal, current->name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == current->name)
        return;
    Topic updated = *current;
    updated.name = name;
    if (!topics.update(updated)) {
        QMessageBox::warning(this, tr("Renombrar tema"),
                             tr("No se pudo renombrar. ¿Ya existe ese tema en la asignatura?"));
        return;
    }
    refreshTopics();
}

void SubjectAdminView::onDeleteTopic() {
    const QUuid id = selectedTopic();
    if (id.isNull())
        return;
    auto* item = m_topics->currentItem();
    if (QMessageBox::question(this, tr("Eliminar tema"),
                              tr("¿Eliminar el tema «%1»?").arg(item->text())) != QMessageBox::Yes)
        return;
    TopicRepository topics(m_db);
    topics.remove(id);
    refreshTopics();
}
