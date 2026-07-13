// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotesView.h"

#include "../theme/Theme.h"
#include "../widgets/NewNoteDialog.h"

#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace pass;

NotesView::NotesView(Database* db, QWidget* parent)
    : QWidget(parent), m_db(db), m_stack(new QStackedWidget), m_list(new QListWidget),
      m_editor(new QPlainTextEdit), m_vaultLabel(new QLabel), m_conflictBar(new QFrame),
      m_deleteButton(new QPushButton(tr("Eliminar nota"))) {
    // Página 0: vault sin configurar.
    auto* emptyPage = new QWidget;
    {
        auto* choose = new QPushButton(tr("Elegir vault de Obsidian..."));
        auto* layout = new QVBoxLayout(emptyPage);
        layout->addStretch();
        auto* label = new QLabel(tr("Las notas se guardan como Markdown dentro de tu vault de "
                                    "Obsidian.\nElige la carpeta del vault para empezar."));
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        layout->addWidget(choose, 0, Qt::AlignCenter);
        layout->addStretch();
        connect(choose, &QPushButton::clicked, this, &NotesView::chooseVault);
    }

    // Página 1: lista + editor con barra de conflicto.
    auto* mainPage = new QWidget;
    {
        QFont mono(pass::theme::bodyFamily());
        mono.setPointSize(11);
        mono.setStyleHint(QFont::Monospace);
        m_editor->setFont(mono);

        // Barra mostrada cuando la nota abierta cambió en Obsidian y aquí hay
        // ediciones sin guardar.
        m_conflictBar->setVisible(false);
        m_conflictBar->setObjectName("conflictBar");
        m_conflictBar->setFrameShape(QFrame::NoFrame);
        {
            auto* text = new QLabel(tr("[ ! ] NOTA MODIFICADA EN OBSIDIAN"));
            text->setObjectName("conflictBarText");
            auto* reload = new QPushButton(tr("Recargar"));
            auto* keep = new QPushButton(tr("Conservar lo mío"));
            auto* layout = new QHBoxLayout(m_conflictBar);
            layout->setContentsMargins(10, 6, 10, 6);
            layout->addWidget(text, 1);
            layout->addWidget(reload);
            layout->addWidget(keep);
            connect(reload, &QPushButton::clicked, this, [this] {
                m_dirty = false;
                reloadCurrentFromDisk();
                m_conflictBar->setVisible(false);
            });
            connect(keep, &QPushButton::clicked, this, [this] {
                m_dirty = true;
                saveCurrent(); // mi versión pisa la del disco
                m_conflictBar->setVisible(false);
            });
        }

        auto* newButton = new QPushButton(tr("Nueva nota"));
        auto* changeVault = new QPushButton(tr("Cambiar vault"));
        m_deleteButton->setEnabled(false);
        m_vaultLabel->setObjectName("hint");
        m_vaultLabel->setWordWrap(true);

        auto* left = new QWidget;
        auto* leftLayout = new QVBoxLayout(left);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->addWidget(pass::theme::sectionLabel(tr("Notas")));
        leftLayout->addWidget(newButton);
        leftLayout->addWidget(m_list, 1);
        leftLayout->addWidget(m_deleteButton);
        leftLayout->addWidget(m_vaultLabel);
        leftLayout->addWidget(changeVault);

        // Enlace a la guía oficial de sintaxis de Obsidian (negrita, fórmulas...).
        auto* help = new QLabel(
            tr("<a href=\"https://help.obsidian.md/syntax\">¿Cómo se escribe en Obsidian? "
               "Guía de formato: negrita, fórmulas, enlaces...</a>"));
        help->setOpenExternalLinks(true);
        help->setObjectName("hint");

        auto* right = new QWidget;
        auto* rightLayout = new QVBoxLayout(right);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->addWidget(m_conflictBar);
        rightLayout->addWidget(m_editor, 1);
        rightLayout->addWidget(help);

        auto* splitter = new QSplitter;
        splitter->addWidget(left);
        splitter->addWidget(right);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 3);

        auto* layout = new QHBoxLayout(mainPage);
        layout->addWidget(splitter);

        connect(newButton, &QPushButton::clicked, this, &NotesView::newNote);
        connect(m_deleteButton, &QPushButton::clicked, this, &NotesView::deleteNote);
        connect(changeVault, &QPushButton::clicked, this, &NotesView::chooseVault);
        connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { loadSelected(); });
    }

    m_stack->addWidget(emptyPage);
    m_stack->addWidget(mainPage);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_stack);

    // Autosave con debounce: escribe 1,5 s después del último cambio.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &NotesView::saveCurrent);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading)
            return;
        m_dirty = true;
        m_saveTimer.start();
    });

    connect(&m_watcher, &VaultWatcher::vaultChanged, this, [this] { refreshList(); });
    connect(&m_watcher, &VaultWatcher::noteChanged, this, &NotesView::onExternalNoteChange);

    rebuildService();
}

void NotesView::hideEvent(QHideEvent* event) {
    saveCurrent();
    QWidget::hideEvent(event);
}

void NotesView::chooseVault() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Elegir vault de Obsidian"),
                                                          m_settings.vaultPath());
    if (dir.isEmpty())
        return;
    m_settings.setVaultPath(dir);
    rebuildService();
    if (m_vault && m_vault->vaultExists() && !m_vault->looksLikeObsidianVault()) {
        QMessageBox::information(
            this, tr("Notas"),
            tr("La carpeta elegida no contiene un vault de Obsidian (falta .obsidian/). "
               "Se usará igualmente, pero comprueba que es la carpeta correcta."));
    }
}

void NotesView::rebuildService() {
    saveCurrent();
    m_currentFile.clear();
    m_conflictBar->setVisible(false);
    m_vault = std::make_unique<VaultService>(m_settings.vaultPath(), m_settings.vaultSubfolder());

    const bool ready = m_vault->vaultExists();
    m_stack->setCurrentIndex(ready ? 1 : 0);
    if (ready) {
        m_vaultLabel->setText(tr("Vault: %1").arg(m_settings.vaultPath()));
        refreshList(/*keepSelection=*/false);
        // El watcher vigila la carpeta de notas (se crea al guardar la primera).
        m_watcher.watch(m_vault->notesDir());
    } else {
        m_watcher.stop();
    }
}

void NotesView::refreshList(bool keepSelection) {
    const QString previous = keepSelection ? m_currentFile : QString();

    m_list->blockSignals(true);
    m_list->clear();
    int restoreRow = -1;
    const auto notes = m_vault->notes();
    for (int i = 0; i < notes.size(); ++i) {
        auto* item = new QListWidgetItem(notes[i].title);
        item->setData(Qt::UserRole, notes[i].fileName);
        item->setToolTip(notes[i].fileName);
        m_list->addItem(item);
        if (notes[i].fileName == previous)
            restoreRow = i;
    }
    if (restoreRow >= 0)
        m_list->setCurrentRow(restoreRow);
    m_list->blockSignals(false);

    if (restoreRow < 0) {
        m_loading = true;
        m_editor->clear();
        m_editor->setEnabled(false);
        m_loading = false;
        m_currentFile.clear();
        m_dirty = false;
        m_conflictBar->setVisible(false);
        m_deleteButton->setEnabled(false);
    }
}

void NotesView::loadSelected() {
    saveCurrent();
    m_conflictBar->setVisible(false);
    auto* item = m_list->currentItem();
    m_deleteButton->setEnabled(item != nullptr);
    if (!item) {
        m_currentFile.clear();
        return;
    }
    m_currentFile = item->data(Qt::UserRole).toString();
    reloadCurrentFromDisk();
}

void NotesView::reloadCurrentFromDisk() {
    if (m_currentFile.isEmpty())
        return;
    const auto content = m_vault->readNote(m_currentFile);
    if (!content) {
        QMessageBox::warning(this, tr("Notas"), tr("No se pudo leer la nota."));
        return;
    }
    m_loading = true;
    m_editor->setPlainText(*content);
    m_editor->setEnabled(true);
    m_loading = false;
    m_dirty = false;
}

void NotesView::onExternalNoteChange(const QString& fileName) {
    if (fileName != m_currentFile) {
        refreshList();
        return;
    }
    const auto onDisk = m_vault->readNote(m_currentFile);
    if (onDisk && *onDisk == m_editor->toPlainText())
        return; // eco de nuestro propio guardado
    if (m_dirty)
        m_conflictBar->setVisible(true);
    else
        reloadCurrentFromDisk();
}

void NotesView::saveCurrent() {
    m_saveTimer.stop();
    if (!m_dirty || m_currentFile.isEmpty() || !m_vault)
        return;
    if (!m_vault->writeNote(m_currentFile, m_editor->toPlainText()))
        QMessageBox::warning(this, tr("Notas"), tr("No se pudo guardar la nota."));
    m_dirty = false;
}

void NotesView::newNote() {
    std::unique_ptr<SubjectRepository> subjects;
    std::unique_ptr<TopicRepository> topics;
    if (m_db && m_db->isOpen()) {
        subjects = std::make_unique<SubjectRepository>(m_db->handle());
        topics = std::make_unique<TopicRepository>(m_db->handle());
    }

    NewNoteDialog dialog(subjects.get(), topics.get(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto fileName = m_vault->createNote(dialog.topic(), dialog.subject());
    if (!fileName) {
        QMessageBox::warning(this, tr("Notas"), tr("No se pudo crear la nota."));
        return;
    }
    // La carpeta de notas puede haberse creado con esta primera nota.
    m_watcher.watch(m_vault->notesDir());
    refreshList(/*keepSelection=*/false);
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == *fileName) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void NotesView::deleteNote() {
    auto* item = m_list->currentItem();
    if (!item)
        return;
    const QString fileName = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(
            this, tr("Eliminar nota"),
            tr("¿Eliminar \"%1\" del vault?\nEsta acción borra el fichero de Obsidian.")
                .arg(item->text())) != QMessageBox::Yes)
        return;

    // La nota desaparece: descarta cualquier autosave pendiente sobre ella.
    m_saveTimer.stop();
    m_dirty = false;
    if (!m_vault->deleteNote(fileName)) {
        QMessageBox::warning(this, tr("Notas"), tr("No se pudo eliminar la nota."));
        return;
    }
    m_currentFile.clear();
    refreshList(/*keepSelection=*/false);
}
