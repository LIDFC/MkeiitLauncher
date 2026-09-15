// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QTimer>

#include "minecraft/auth/AuthStep.h"
#include "net/NetJob.h"
#include "net/Request.h"

/**
 * Logs in to Ely.by using the OAuth2 device authorization grant (RFC 8628).
 * The user confirms the login in their browser, the launcher never sees the password.
 */
class ElyByDeviceCodeStep : public AuthStep {
    Q_OBJECT
   public:
    explicit ElyByDeviceCodeStep(AccountData* data);
    ~ElyByDeviceCodeStep() noexcept override = default;

    void perform() override;

    QString describe() override;

   public slots:
    void abort() override;

   signals:
    void authorizeWithBrowser(QString url, QString code, int expiresIn);

   private slots:
    void deviceAuthorizationFinished(QByteArray* response);
    void startPollTimer();
    void authenticateUser();
    void authenticationFinished(QByteArray* response);

   private:
    QString m_clientId;
    QString m_deviceCode;
    bool m_isAborted = false;
    int m_interval = 5;

    QTimer m_pollTimer;
    QTimer m_expirationTimer;

    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};
