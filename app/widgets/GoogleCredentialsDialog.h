// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QLineEdit;

// Pop-up para introducir las credenciales del OAuth Client de Google Cloud
// (Client ID + Client secret). No persiste nada: el llamante decide qué hacer
// con los valores (GoogleAuthService::setClientCredentials). El secret va en modo
// password y nunca se precarga con el valor guardado.
class GoogleCredentialsDialog : public QDialog {
    Q_OBJECT

public:
    explicit GoogleCredentialsDialog(QWidget* parent = nullptr);

    QString clientId() const;
    QString clientSecret() const;

private:
    void accept() override;

    QLineEdit* m_clientId;
    QLineEdit* m_clientSecret;
};
