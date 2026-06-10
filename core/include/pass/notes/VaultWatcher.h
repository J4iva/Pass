// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace pass {

// Vigila la carpeta de notas del vault y avisa de cambios hechos fuera de la
// app (p. ej. desde Obsidian).
//
// Particularidades de QFileSystemWatcher en Windows que se compensan aquí:
// - varios avisos por un solo guardado → debounce de 500 ms
// - los editores guardan con rename (replace), lo que invalida el watch del
//   fichero → se re-añaden los paths tras cada notificación
class VaultWatcher : public QObject {
    Q_OBJECT

public:
    explicit VaultWatcher(QObject* parent = nullptr);

    void watch(const QString& notesDir);
    void stop();

signals:
    // Altas/bajas de notas en la carpeta (o cambios masivos).
    void vaultChanged();
    // Contenido de una nota concreta modificado externamente.
    void noteChanged(const QString& fileName);

private:
    void onDirectoryChanged();
    void onFileChanged(const QString& path);
    void rewatchFiles();
    void flush();

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_dir;
    QSet<QString> m_changedFiles;
    bool m_dirChanged = false;
};

} // namespace pass
