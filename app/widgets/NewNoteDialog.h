// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QDialog>

class QComboBox;

// Alta de nota: pide asignatura y/o tema (al menos uno). Si hay base de
// datos, ofrece las asignaturas existentes y, al elegir una, los temas ya
// creados para ella. Tanto asignaturas como temas nuevos se crean al aceptar.
class NewNoteDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewNoteDialog(pass::SubjectRepository* subjects, pass::TopicRepository* topics,
                           QWidget* parent = nullptr);

    QString subject() const;
    QString topic() const;

    void accept() override;

private:
    // Recarga la lista de temas del combo según la asignatura seleccionada.
    void reloadTopics();

    pass::SubjectRepository* m_subjects;
    pass::TopicRepository* m_topics;
    QComboBox* m_subject;
    QComboBox* m_topic;
};
