// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotesView.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace pass;

NotesView::NotesView(QWidget* parent)
    : QWidget(parent), m_stack(new QStackedWidget), m_list(new QListWidget),
      m_editor(new QPlainTextEdit), m_vaultLabel(new QLabel) {
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

    // Página 1: lista + editor.
    auto* mainPage = new QWidget;
    {
        QFont mono(QStringLiteral("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        m_editor->setFont(mono);

        auto* newButton = new QPushButton(tr("Nueva nota"));
        auto* changeVault = new QPushButton(tr("Cambiar vault"));
        m_vaultLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
        m_vaultLabel->setWordWrap(true);

        auto* left = new QWidget;
        auto* leftLayout = new QVBoxLayout(left);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->addWidget(newButton);
        leftLayout->addWidget(m_list, 1);
        leftLayout->addWidget(m_vaultLabel);
        leftLayout->addWidget(changeVault);

        auto* splitter = new QSplitter;
        splitter->addWidget(left);
        splitter->addWidget(m_editor);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 3);

        auto* layout = new QHBoxLayout(mainPage);
        layout->addWidget(splitter);

        connect(newButton, &QPushButton::clicked, this, &NotesView::newNote);
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
    if (m_vault && !m_vault->looksLikeObsidianVault()) {
        QMessageBox::information(
            this, tr("Notas"),
            tr("La carpeta elegida no contiene un vault de Obsidian (falta .obsidian/). "
               "Se usará igualmente, pero comprueba que es la carpeta correcta."));
    }
}

void NotesView::rebuildService() {
    saveCurrent();
    m_currentFile.clear();
    m_vault = std::make_unique<VaultService>(m_settings.vaultPath(), m_settings.vaultSubfolder());

    const bool ready = m_vault->vaultExists();
    m_stack->setCurrentIndex(ready ? 1 : 0);
    if (ready) {
        m_vaultLabel->setText(tr("Vault: %1").arg(m_settings.vaultPath()));
        refreshList();
    }
}

void NotesView::refreshList() {
    m_list->blockSignals(true);
    m_list->clear();
    for (const auto& note : m_vault->notes()) {
        auto* item = new QListWidgetItem(note.title);
        item->setData(Qt::UserRole, note.fileName);
        item->setToolTip(note.fileName);
        m_list->addItem(item);
    }
    m_list->blockSignals(false);

    m_loading = true;
    m_editor->clear();
    m_editor->setEnabled(false);
    m_loading = false;
    m_currentFile.clear();
}

void NotesView::loadSelected() {
    saveCurrent();
    auto* item = m_list->currentItem();
    if (!item) {
        m_currentFile.clear();
        return;
    }
    const QString fileName = item->data(Qt::UserRole).toString();
    const auto content = m_vault->readNote(fileName);
    if (!content) {
        QMessageBox::warning(this, tr("Notas"), tr("No se pudo leer la nota."));
        return;
    }
    m_loading = true;
    m_editor->setPlainText(*content);
    m_editor->setEnabled(true);
    m_loading = false;
    m_currentFile = fileName;
    m_dirty = false;
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
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Nueva nota"), tr("Título:"),
                                                QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    const auto fileName = m_vault->createNote(title);
    if (!fileName) {
        QMessageBox::warning(this, tr("Notas"), tr("No se pudo crear la nota."));
        return;
    }
    refreshList();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == *fileName) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}
