// SPDX-License-Identifier: GPL-3.0-only
#include "ElyByProfileStep.h"

#include "Application.h"
#include "minecraft/auth/ElyBy.h"
#include "minecraft/auth/Parsers.h"
#include "net/NetUtils.h"
#include "net/RawHeaderProxy.h"

ElyByProfileStep::ElyByProfileStep(AccountData* data) : AuthStep(data) {}

QString ElyByProfileStep::describe()
{
    return tr("Fetching the Ely.by profile.");
}

void ElyByProfileStep::perform()
{
    auto headers = QList<Net::HeaderPair>{ { "Accept", "application/json" },
                                           { "Authorization", QString("Bearer %1").arg(m_data->elyToken.token).toUtf8() } };

    auto [request, response] = Net::Request::makeByteArray(QUrl(ElyBy::ACCOUNT_INFO_URL));
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(headers));
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("ElyByProfileStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });

    m_task->start();
}

void ElyByProfileStep::abort()
{
    if (m_task) {
        m_task->abort();
    }
}

void ElyByProfileStep::onRequestDone(QByteArray* response)
{
    const auto error = m_request->error();
    if (error != QNetworkReply::NoError) {
        qWarning() << "Error getting Ely.by profile:";
        qWarning() << " HTTP Status       :" << m_request->replyStatusCode();
        qWarning() << " Internal error no.:" << error;
        qWarning() << " Error string      :" << m_request->errorString();

        if (error == QNetworkReply::AuthenticationRequiredError || m_request->replyStatusCode() == 401) {
            emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Ely.by session has expired."));
        } else if (Net::isApplicationError(error) && !Net::isServerError(error)) {
            emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("Ely.by profile acquisition failed: %1").arg(m_request->errorString()));
        } else {
            m_data->networkError = error;
            emit finished(AccountTaskState::STATE_OFFLINE, tr("Ely.by profile acquisition failed: %1").arg(m_request->errorString()));
        }
        return;
    }

    if (!Parsers::parseElyByAccountInfo(*response, m_data->minecraftProfile)) {
        emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("Ely.by profile response could not be parsed"));
        return;
    }

    // With the minecraft_server_session scope the OAuth2 access token is also the game session token
    // (verified by Ely.by's session server through authlib-injector). It is not persisted twice.
    m_data->yggdrasilToken = m_data->elyToken;
    m_data->yggdrasilToken.refresh_token.clear();
    m_data->yggdrasilToken.persistent = false;

    emit finished(AccountTaskState::STATE_WORKING, tr("Got Ely.by profile"));
}
