// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/repo/SubjectRepository.h"

#include <QDialog>

class QComboBox;
class QLineEdit;

// Alta de nota: pide asignatura y/o tema (al menos uno). Si hay base de
// datos, ofrece las asignaturas existentes y crea las nuevas al aceptar.
class NewNoteDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewNoteDialog(pass::SubjectRepository* subjects, QWidget* parent = nullptr);

    QString subject() const;
    QString topic() const;

    void accept() override;

private:
    pass::SubjectRepository* m_subjects;
    QComboBox* m_subject;
    QLineEdit* m_topic;
};
