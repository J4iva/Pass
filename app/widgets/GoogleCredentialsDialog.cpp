// SPDX-License-Identifier: GPL-3.0-or-later
#include "GoogleCredentialsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

GoogleCredentialsDialog::GoogleCredentialsDialog(QWidget* parent)
    : QDialog(parent), m_clientId(new QLineEdit), m_clientSecret(new QLineEdit) {
    setWindowTitle(tr("Credenciales de Google Cloud"));
    setMinimumWidth(420);

    m_clientSecret->setEchoMode(QLineEdit::Password);
    m_clientId->setPlaceholderText(tr("xxxx.apps.googleusercontent.com"));
    m_clientSecret->setPlaceholderText(tr("client secret"));

    auto* form = new QFormLayout;
    form->addRow(tr("Client ID"), m_clientId);
    form->addRow(tr("Client secret"), m_clientSecret);

    auto* help =
        new QLabel(tr("Crea un OAuth Client de tipo «Aplicación de escritorio» en Google Cloud "
                      "Console y pega aquí sus datos. Guía: docs/google-calendar.md."));
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(help);
    layout->addWidget(buttons);
}

QString GoogleCredentialsDialog::clientId() const {
    return m_clientId->text().trimmed();
}

QString GoogleCredentialsDialog::clientSecret() const {
    return m_clientSecret->text();
}

void GoogleCredentialsDialog::accept() {
    if (clientId().isEmpty() || clientSecret().isEmpty()) {
        QMessageBox::warning(this, tr("Credenciales"),
                             tr("Rellena el Client ID y el Client secret."));
        return;
    }
    QDialog::accept();
}
