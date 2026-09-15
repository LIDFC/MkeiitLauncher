// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>

#include "minecraft/auth/AuthStep.h"
#include "net/NetJob.h"
#include "net/Request.h"

/**
 * Fetches the Ely.by account (username and UUID) and prepares the game session token.
 */
class ElyByProfileStep : public AuthStep {
    Q_OBJECT
   public:
    explicit ElyByProfileStep(AccountData* data);
    ~ElyByProfileStep() noexcept override = default;

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
