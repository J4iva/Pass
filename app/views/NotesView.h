// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/notes/VaultService.h"
#include "pass/settings/AppSettings.h"

#include <QTimer>
#include <QWidget>

#include <memory>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;

class NotesView : public QWidget {
    Q_OBJECT

public:
    explicit NotesView(QWidget* parent = nullptr);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void chooseVault();
    void rebuildService();
    void refreshList();
    void loadSelected();
    void saveCurrent();
    void newNote();

    pass::AppSettings m_settings;
    std::unique_ptr<pass::VaultService> m_vault;
    QString m_currentFile;
    bool m_loading = false;
    bool m_dirty = false;
    QTimer m_saveTimer;

    QStackedWidget* m_stack;
    QListWidget* m_list;
    QPlainTextEdit* m_editor;
    QLabel* m_vaultLabel;
};
