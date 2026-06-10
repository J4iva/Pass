// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/db/Database.h"
#include "pass/notes/VaultService.h"
#include "pass/notes/VaultWatcher.h"
#include "pass/settings/AppSettings.h"

#include <QTimer>
#include <QWidget>

#include <memory>

class QFrame;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

class NotesView : public QWidget {
    Q_OBJECT

public:
    // db puede ser nulo (sin asignaturas sugeridas en el alta de notas).
    explicit NotesView(pass::Database* db = nullptr, QWidget* parent = nullptr);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void chooseVault();
    void rebuildService();
    void refreshList(bool keepSelection = true);
    void loadSelected();
    void saveCurrent();
    void newNote();
    void deleteNote();
    void onExternalNoteChange(const QString& fileName);
    void reloadCurrentFromDisk();

    pass::Database* m_db;
    pass::AppSettings m_settings;
    std::unique_ptr<pass::VaultService> m_vault;
    pass::VaultWatcher m_watcher;
    QString m_currentFile;
    bool m_loading = false;
    bool m_dirty = false;
    QTimer m_saveTimer;

    QStackedWidget* m_stack;
    QListWidget* m_list;
    QPlainTextEdit* m_editor;
    QLabel* m_vaultLabel;
    QFrame* m_conflictBar;
    QPushButton* m_deleteButton;
};
