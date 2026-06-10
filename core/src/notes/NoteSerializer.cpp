// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/notes/NoteSerializer.h"

namespace pass {

namespace {
const QString kMarker = QStringLiteral("---");
}

NoteSerializer::Document NoteSerializer::parse(const QString& content) {
    Document doc;
    // El frontmatter debe empezar en la primera línea del fichero.
    if (!content.startsWith(kMarker + QLatin1Char('\n')) &&
        !content.startsWith(kMarker + QStringLiteral("\r\n"))) {
        doc.body = content;
        return doc;
    }

    const QStringList lines = content.split(QLatin1Char('\n'));
    int closing = -1;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed() == kMarker) {
            closing = i;
            break;
        }
    }
    if (closing < 0) {
        doc.body = content;
        return doc;
    }

    doc.hasFrontmatter = true;
    for (int i = 1; i < closing; ++i)
        doc.frontmatter << QString(lines[i]).remove(QLatin1Char('\r'));
    doc.body = lines.mid(closing + 1).join(QLatin1Char('\n'));
    return doc;
}

QString NoteSerializer::serialize(const Document& doc) {
    if (!doc.hasFrontmatter)
        return doc.body;
    return kMarker + QLatin1Char('\n') + doc.frontmatter.join(QLatin1Char('\n')) +
           QLatin1Char('\n') + kMarker + QLatin1Char('\n') + doc.body;
}

QString NoteSerializer::value(const Document& doc, const QString& key) {
    const QString prefix = key + QStringLiteral(":");
    for (const QString& line : doc.frontmatter) {
        if (line.startsWith(prefix))
            return line.mid(prefix.size()).trimmed();
    }
    return {};
}

void NoteSerializer::setValue(Document& doc, const QString& key, const QString& value) {
    doc.hasFrontmatter = true;
    const QString prefix = key + QStringLiteral(":");
    const QString newLine = prefix + QLatin1Char(' ') + value;
    for (QString& line : doc.frontmatter) {
        if (line.startsWith(prefix)) {
            line = newLine;
            return;
        }
    }
    doc.frontmatter << newLine;
}

} // namespace pass
