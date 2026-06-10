// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/Note.h"

#include <QList>
#include <QString>

#include <optional>

namespace pass {

// Lee y escribe notas Markdown en una subcarpeta del vault de Obsidian.
// Escrituras atómicas con QSaveFile para no corromper notas a medio guardar.
class VaultService {
public:
    VaultService(QString vaultPath, QString subfolder);

    bool vaultExists() const;
    bool looksLikeObsidianVault() const; // tiene carpeta .obsidian/
    QString notesDir() const;
    QString filePathFor(const QString& fileName) const;

    QList<Note> notes() const; // más recientes primero
    std::optional<QString> readNote(const QString& fileName) const;
    bool writeNote(const QString& fileName, const QString& content);
    bool deleteNote(const QString& fileName);
    // Crea una nota con frontmatter de la app y fecha registrada. Con
    // asignatura genera plantilla de estudio (Apuntes/Dudas); sin ella, una
    // nota libre. Devuelve el nombre de fichero.
    std::optional<QString> createNote(const QString& topic, const QString& subject = {});

    static QString sanitizeTitle(const QString& title);

private:
    QString m_vaultPath;
    QString m_subfolder;
};

} // namespace pass
