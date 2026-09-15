// SPDX-License-Identifier: GPL-3.0-only
#include "ElyByRefreshStep.h"

#include <QDateTime>
#include <QUrlQuery>

#include "Application.h"
#include "minecraft/auth/ElyBy.h"
#include "minecraft/auth/Parsers.h"
#include "net/NetUtils.h"
#include "net/RawHeaderProxy.h"

ElyByRefreshStep::ElyByRefreshStep(AccountData* data) : AuthStep(data) {}

QString ElyByRefreshStep::describe()
{
    return tr("Refreshing Ely.by session.");
}

void ElyByRefreshStep::perform()
{
    const auto clientId = APPLICATION->getElyByClientID();
    if (clientId.isEmpty() || m_data->elyClientID != clientId) {
        emit finished(AccountTaskState::STATE_DISABLED, tr("Ely.by authentication failed - client identification has changed."));
        return;
    }
    if (m_data->elyToken.refresh_token.isEmpty()) {
        emit finished(AccountTaskState::STATE_DISABLED, tr("Ely.by authentication failed - refresh token is empty."));
        return;
    }

    QUrlQuery data;
    data.addQueryItem("client_id", clientId);
    data.addQueryItem("grant_type", "refresh_token");
    data.addQueryItem("refresh_token", m_data->elyToken.refresh_token);
    data.addQueryItem("scope", ElyBy::OAUTH_SCOPES);
    auto payload = data.query(QUrl::FullyEncoded).toUtf8();

    auto headers = QList<Net::HeaderPair>{
        { "Content-Type", "application/x-www-form-urlencoded" },
        { "Accept", "application/json" },
    };
    auto [request, response] = Net::Request::makeByteArray(QUrl(ElyBy::OAUTH_TOKEN_URL), payload);
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(headers));
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("ElyByRefreshStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });

    m_task->start();
}

void ElyByRefreshStep::abort()
{
    if (m_task) {
        m_task->abort();
    }
}

void ElyByRefreshStep::onRequestDone(QByteArray* response)
{
    auto rsp = Parsers::parseOAuthTokenResponse(*response);
    const auto error = m_request->error();

    if (error != QNetworkReply::NoError || rsp.accessToken.isEmpty()) {
        qWarning() << "Ely.by token refresh failed:" << error << m_request->errorString() << rsp.error;
        if (!rsp.error.isEmpty()) {
            // the authorization server rejected the refresh token (e.g. revoked by the user)
            emit finished(AccountTaskState::STATE_FAILED_HARD,
                          tr("Ely.by session has expired: %1").arg(rsp.errorDescription.isEmpty() ? rsp.error : rsp.errorDescription));
        } else if (error == QNetworkReply::NoError || (Net::isApplicationError(error) && !Net::isServerError(error))) {
            emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("Failed to refresh Ely.by token: %1").arg(m_request->errorString()));
        } else {
            m_data->networkError = error;
            emit finished(AccountTaskState::STATE_OFFLINE, tr("Failed to refresh Ely.by token: %1").arg(m_request->errorString()));
        }
        return;
    }

    auto now = QDateTime::currentDateTimeUtc();
    m_data->elyToken.issueInstant = now;
    m_data->elyToken.notAfter = rsp.expiresIn > 0 ? now.addSecs(rsp.expiresIn) : QDateTime();
    m_data->elyToken.token = rsp.accessToken;
    if (!rsp.refreshToken.isEmpty()) {
        m_data->elyToken.refresh_token = rsp.refreshToken;
    }
    m_data->elyToken.validity = Validity::Certain;
    emit finished(AccountTaskState::STATE_WORKING, tr("Got Ely.by token"));
}
