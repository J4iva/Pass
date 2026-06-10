// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/notes/VaultWatcher.h"

#include <QDir>
#include <QFileInfo>

namespace pass {

VaultWatcher::VaultWatcher(QObject* parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(500);
    connect(&m_debounce, &QTimer::timeout, this, &VaultWatcher::flush);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { onDirectoryChanged(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &VaultWatcher::onFileChanged);
}

void VaultWatcher::watch(const QString& notesDir) {
    stop();
    m_dir = notesDir;
    if (m_dir.isEmpty() || !QDir(m_dir).exists())
        return;
    m_watcher.addPath(m_dir);
    rewatchFiles();
}

void VaultWatcher::stop() {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_debounce.stop();
    m_changedFiles.clear();
    m_dirChanged = false;
    m_dir.clear();
}

void VaultWatcher::onDirectoryChanged() {
    m_dirChanged = true;
    rewatchFiles();
    m_debounce.start();
}

void VaultWatcher::onFileChanged(const QString& path) {
    m_changedFiles.insert(QFileInfo(path).fileName());
    // Tras un rename-on-save el path puede haber salido del watcher: re-añadir.
    if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    m_debounce.start();
}

void VaultWatcher::rewatchFiles() {
    if (m_dir.isEmpty())
        return;
    const QStringList watched = m_watcher.files();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    QStringList paths;
    for (const auto& info :
         QDir(m_dir).entryInfoList({QStringLiteral("*.md")}, QDir::Files))
        paths << info.absoluteFilePath();
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}

void VaultWatcher::flush() {
    const bool dirChanged = m_dirChanged;
    const QSet<QString> files = m_changedFiles;
    m_dirChanged = false;
    m_changedFiles.clear();

    if (dirChanged)
        emit vaultChanged();
    for (const QString& fileName : files)
        emit noteChanged(fileName);
}

} // namespace pass
