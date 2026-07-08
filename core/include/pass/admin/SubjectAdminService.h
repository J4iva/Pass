// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QUuid>

namespace pass {

class CalendarService;

// Operaciones de Administración sobre asignaturas que tocan varias fuentes a la
// vez (BD + notas en el vault) y deben mantenerlas coherentes:
//   - renombrar: actualiza la asignatura (eventos/sesiones la referencian por id,
//     así que su nombre mostrado cambia solo) y reescribe las notas .md que la
//     llevan embebida (frontmatter, tag, cuerpo y nombre de fichero).
//   - eliminar: borra tareas ([T]) y notas de la asignatura y sus temas; conserva
//     el histórico de estudio dejando sus sesiones y eventos sin asignatura.
// Sin Qt Widgets, para respetar la regla de passcore.
class SubjectAdminService {
public:
    struct Impact {
        int events = 0;   // eventos (incluye tareas) con esta asignatura
        int tasks = 0;    // de esos, cuántos son tareas ([T])
        int sessions = 0; // sesiones de estudio con esta asignatura
        int notes = 0;    // notas .md del vault con esta asignatura
        int topics = 0;   // temas de la asignatura
    };

    SubjectAdminService(QSqlDatabase db, QString vaultPath, QString vaultSubfolder,
                        CalendarService* calendar = nullptr);

    Impact impactOf(const QUuid& subjectId) const;

    // Renombra la asignatura y propaga el cambio a sus notas. `error` recibe un
    // mensaje legible si falla (nombre vacío/duplicado, etc.).
    bool rename(const QUuid& id, const QString& newName, QString& error);

    // Elimina la asignatura en cascada (ver descripción de la clase).
    bool remove(const QUuid& id, QString& error);

private:
    int rewriteNotesSubject(const QString& oldName, const QString& newName); // nº de notas tocadas
    QStringList notesForSubject(const QString& name) const;

    QSqlDatabase m_db;
    QString m_vaultPath;
    QString m_vaultSubfolder;
    CalendarService* m_calendar;
};

} // namespace pass
