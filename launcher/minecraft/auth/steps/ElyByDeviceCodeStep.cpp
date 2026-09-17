// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2024 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ElyByDeviceCodeStep.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include "Application.h"
#include "minecraft/auth/ElyBy.h"
#include "minecraft/auth/Parsers.h"
#include "net/RawHeaderProxy.h"

namespace {
struct DeviceAuthorizationResponse {
    QString device_code;
    QString user_code;
    QString verification_uri;
    QString verification_uri_complete;
    int expires_in = 0;
    int interval = 0;

    QString error;
    QString error_description;
};

DeviceAuthorizationResponse parseDeviceAuthorizationResponse(const QByteArray& data)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse device authorization response due to err:" << err.errorString();
        return {};
    }

    if (!doc.isObject()) {
        qWarning() << "Device authorization response is not an object";
        return {};
    }
    auto obj = doc.object();
    DeviceAuthorizationResponse rsp;
    rsp.device_code = obj["device_code"].toString();
    rsp.user_code = obj["user_code"].toString();
    rsp.verification_uri = obj["verification_uri"].toString();
    rsp.verification_uri_complete = obj["verification_uri_complete"].toString();
    rsp.expires_in = obj["expires_in"].toInt();
    rsp.interval = obj["interval"].toInt();
    rsp.error = obj["error"].toString();
    rsp.error_description = obj["error_description"].toString();
    return rsp;
}

QList<Net::HeaderPair> formHeaders()
{
    return {
        { "Content-Type", "application/x-www-form-urlencoded" },
        { "Accept", "application/json" },
    };
}
}  // namespace

// https://datatracker.ietf.org/doc/html/rfc8628
ElyByDeviceCodeStep::ElyByDeviceCodeStep(AccountData* data) : AuthStep(data)
{
    m_clientId = APPLICATION->getElyByClientID();
    connect(&m_expirationTimer, &QTimer::timeout, this, &ElyByDeviceCodeStep::abort);
    connect(&m_pollTimer, &QTimer::timeout, this, &ElyByDeviceCodeStep::authenticateUser);
}

QString ElyByDeviceCodeStep::describe()
{
    return tr("Logging in with Ely.by account.");
}

void ElyByDeviceCodeStep::perform()
{
    if (m_clientId.isEmpty()) {
        emit finished(AccountTaskState::STATE_FAILED_HARD,
                      tr("No Ely.by client ID is configured. Set one in Settings > APIs or build the launcher with "
                         "Launcher_ELYBY_CLIENT_ID."));
        return;
    }

    QUrlQuery data;
    data.addQueryItem("client_id", m_clientId);
    data.addQueryItem("scope", ElyBy::OAUTH_SCOPES);
    auto payload = data.query(QUrl::FullyEncoded).toUtf8();
    auto [request, response] = Net::Request::makeByteArray(QUrl(ElyBy::OAUTH_DEVICE_CODE_URL), payload);
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(formHeaders()));
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("ElyByDeviceCodeStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { deviceAuthorizationFinished(response); });

    m_task->start();
}

void ElyByDeviceCodeStep::deviceAuthorizationFinished(QByteArray* response)
{
    auto rsp = parseDeviceAuthorizationResponse(*response);
    if (!rsp.error.isEmpty() || !rsp.error_description.isEmpty()) {
        qWarning() << "Device authorization failed:" << rsp.error;
        emit finished(AccountTaskState::STATE_FAILED_HARD,
                      tr("Device authorization failed: %1").arg(rsp.error_description.isEmpty() ? rsp.error : rsp.error_description));
        return;
    }
    // Net::Request never switches its task state to Succeeded, so the network error is the only reliable signal
    if (m_request->error() != QNetworkReply::NoError) {
        qWarning() << "Device authorization failed:" << m_request->error() << m_request->errorString();
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Device authorization failed: %1").arg(m_request->errorString()));
        return;
    }
    if (rsp.device_code.isEmpty() || rsp.user_code.isEmpty() || rsp.verification_uri.isEmpty() || rsp.expires_in == 0) {
        qWarning() << "Device authorization failed: required fields missing";
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Device authorization failed: required fields missing"));
        return;
    }
    if (rsp.interval != 0) {
        m_interval = rsp.interval;
    }
    m_deviceCode = rsp.device_code;
    emit authorizeWithBrowser(rsp.verification_uri_complete.isEmpty() ? rsp.verification_uri : rsp.verification_uri_complete, rsp.user_code,
                              rsp.expires_in);
    m_expirationTimer.setTimerType(Qt::VeryCoarseTimer);
    m_expirationTimer.setInterval(rsp.expires_in * 1000);
    m_expirationTimer.setSingleShot(true);
    m_expirationTimer.start();
    m_pollTimer.setTimerType(Qt::VeryCoarseTimer);
    m_pollTimer.setSingleShot(true);
    startPollTimer();
}

void ElyByDeviceCodeStep::abort()
{
    m_expirationTimer.stop();
    m_pollTimer.stop();
    if (m_request) {
        m_request->abort();
    }
    m_isAborted = true;
}

void ElyByDeviceCodeStep::startPollTimer()
{
    if (m_isAborted) {
        return;
    }
    if (m_expirationTimer.remainingTime() < m_interval * 1000) {
        // the code is about to expire, request a new one
        perform();
        return;
    }

    m_pollTimer.setInterval(m_interval * 1000);
    m_pollTimer.start();
}

void ElyByDeviceCodeStep::authenticateUser()
{
    QUrlQuery data;
    data.addQueryItem("client_id", m_clientId);
    data.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    data.addQueryItem("device_code", m_deviceCode);
    auto payload = data.query(QUrl::FullyEncoded).toUtf8();
    auto [request, response] = Net::Request::makeByteArray(QUrl(ElyBy::OAUTH_TOKEN_URL), payload);
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(formHeaders()));

    connect(m_request.get(), &Task::finished, this, [this, response] { authenticationFinished(response); });

    m_request->setNetwork(APPLICATION->network());
    m_request->start();
}

void ElyByDeviceCodeStep::authenticationFinished(QByteArray* response)
{
    if (m_request->error() == QNetworkReply::TimeoutError) {
        // rfc8628#section-3.5: back off on connection timeouts
        m_interval *= 2;
        startPollTimer();
        return;
    }
    auto rsp = Parsers::parseOAuthTokenResponse(*response);
    if (rsp.error == "slow_down") {
        // rfc8628#section-3.5: the interval MUST be increased by 5 seconds
        m_interval += 5;
        startPollTimer();
        return;
    }
    if (rsp.error == "authorization_pending") {
        // the user has not confirmed the login yet
        startPollTimer();
        return;
    }
    if (!rsp.error.isEmpty() || !rsp.errorDescription.isEmpty()) {
        qWarning() << "Device access failed:" << rsp.error;
        emit finished(AccountTaskState::STATE_FAILED_HARD,
                      tr("Ely.by login failed: %1").arg(rsp.errorDescription.isEmpty() ? rsp.error : rsp.errorDescription));
        return;
    }
    if (m_request->error() != QNetworkReply::NoError) {
        startPollTimer();  // it failed so just try again without increasing the interval
        return;
    }
    if (rsp.accessToken.isEmpty()) {
        m_expirationTimer.stop();
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Ely.by login failed: no access token was returned"));
        return;
    }

    m_expirationTimer.stop();
    auto now = QDateTime::currentDateTimeUtc();
    m_data->elyClientID = m_clientId;
    m_data->elyToken.issueInstant = now;
    m_data->elyToken.notAfter = rsp.expiresIn > 0 ? now.addSecs(rsp.expiresIn) : QDateTime();
    m_data->elyToken.token = rsp.accessToken;
    m_data->elyToken.refresh_token = rsp.refreshToken;
    m_data->elyToken.extra.clear();
    m_data->elyToken.validity = Validity::Certain;
    emit finished(AccountTaskState::STATE_WORKING, tr("Got Ely.by token"));
}
