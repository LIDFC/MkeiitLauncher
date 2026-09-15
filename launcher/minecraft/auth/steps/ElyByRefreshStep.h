// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>

#include "minecraft/auth/AuthStep.h"
#include "net/NetJob.h"
#include "net/Request.h"

/**
 * Refreshes the Ely.by OAuth2 access token using the stored refresh token (offline_access scope).
 */
class ElyByRefreshStep : public AuthStep {
    Q_OBJECT
   public:
    explicit ElyByRefreshStep(AccountData* data);
    ~ElyByRefreshStep() noexcept override = default;

    void perform() override;

    QString describe() override;

   public slots:
    void abort() override;

   private slots:
    void onRequestDone(QByteArray* response);

   private:
    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};
