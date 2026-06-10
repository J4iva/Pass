// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/notes/VaultService.h"

#include "pass/notes/NoteSerializer.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace pass {

namespace {

// "2026-06-10 1730 - Título.md" → "Título"
QString titleFromFileName(const QString& fileName) {
    QString base = fileName;
    base.chop(3); // ".md"
    static const QRegularExpression datePrefix(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2} \d{4} - )"));
    return base.remove(datePrefix);
}

} // namespace

VaultService::VaultService(QString vaultPath, QString subfolder)
    : m_vaultPath(std::move(vaultPath)), m_subfolder(std::move(subfolder)) {}

bool VaultService::vaultExists() const {
    return !m_vaultPath.isEmpty() && QDir(m_vaultPath).exists();
}

bool VaultService::looksLikeObsidianVault() const {
    return QDir(m_vaultPath + QStringLiteral("/.obsidian")).exists();
}

QString VaultService::notesDir() const {
    return m_subfolder.isEmpty() ? m_vaultPath : m_vaultPath + QLatin1Char('/') + m_subfolder;
}

QString VaultService::filePathFor(const QString& fileName) const {
    return notesDir() + QLatin1Char('/') + fileName;
}

QList<Note> VaultService::notes() const {
    QList<Note> result;
    QDirIterator it(notesDir(), {QStringLiteral("*.md")}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        result.append({info.fileName(), titleFromFileName(info.fileName()),
                       info.lastModified()});
    }
    std::sort(result.begin(), result.end(),
              [](const Note& a, const Note& b) { return a.modified > b.modified; });
    return result;
}

std::optional<QString> VaultService::readNote(const QString& fileName) const {
    QFile file(filePathFor(fileName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    QTextStream in(&file);
    return in.readAll();
}

bool VaultService::writeNote(const QString& fileName, const QString& content) {
    if (!vaultExists())
        return false;
    QDir().mkpath(notesDir());
    QSaveFile file(filePathFor(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out << content;
    out.flush();
    return file.commit();
}

bool VaultService::deleteNote(const QString& fileName) {
    if (fileName.isEmpty())
        return false;
    return QFile::remove(filePathFor(fileName));
}

std::optional<QString> VaultService::createNote(const QString& topic, const QString& subject) {
    if (!vaultExists())
        return std::nullopt;

    const QString safeTopic = sanitizeTitle(topic);
    const QString safeSubject = sanitizeTitle(subject);
    const QDateTime now = QDateTime::currentDateTime();

    QStringList stemParts{now.toString(QStringLiteral("yyyy-MM-dd HHmm"))};
    if (!safeSubject.isEmpty())
        stemParts << safeSubject;
    stemParts << (safeTopic.isEmpty() ? (safeSubject.isEmpty() ? QStringLiteral("Nota")
                                                               : safeSubject)
                                      : safeTopic);
    const QString stem = stemParts.join(QStringLiteral(" - "));

    QDir().mkpath(notesDir());
    QString fileName = stem + QStringLiteral(".md");
    for (int n = 2; QFile::exists(filePathFor(fileName)); ++n)
        fileName = QStringLiteral("%1 (%2).md").arg(stem).arg(n);

    NoteSerializer::Document doc;
    NoteSerializer::setValue(doc, QStringLiteral("created"), now.toString(Qt::ISODate));
    NoteSerializer::setValue(doc, QStringLiteral("app"), QStringLiteral("pass"));

    const QString heading = safeTopic.isEmpty() ? (safeSubject.isEmpty() ? QStringLiteral("Nota")
                                                                         : safeSubject)
                                                : topic.trimmed();
    const QString dateText = now.toString(QStringLiteral("dd/MM/yyyy HH:mm"));

    if (!safeSubject.isEmpty()) {
        // Nota de estudio: plantilla con secciones.
        NoteSerializer::setValue(doc, QStringLiteral("subject"), subject.trimmed());
        QString subjectTag = safeSubject.toLower();
        subjectTag.replace(QLatin1Char(' '), QLatin1Char('-'));
        NoteSerializer::setValue(doc, QStringLiteral("tags"),
                                 QStringLiteral("[pass, %1]").arg(subjectTag));
        doc.body = QStringLiteral("\n# %1\n\n> Asignatura: %2 · Creada: %3\n\n"
                                  "## Apuntes\n\n\n## Dudas\n\n")
                       .arg(heading, subject.trimmed(), dateText);
    } else {
        // Nota libre: estructura mínima.
        NoteSerializer::setValue(doc, QStringLiteral("tags"), QStringLiteral("[pass]"));
        doc.body = QStringLiteral("\n# %1\n\n> Creada: %2\n\n").arg(heading, dateText);
    }

    if (!writeNote(fileName, NoteSerializer::serialize(doc)))
        return std::nullopt;
    return fileName;
}

QString VaultService::sanitizeTitle(const QString& title) {
    QString safe = title.trimmed();
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|#^\[\]])"));
    safe.remove(forbidden);
    safe = safe.simplified();
    if (safe.size() > 80)
        safe.truncate(80);
    return safe.trimmed();
}

} // namespace pass
