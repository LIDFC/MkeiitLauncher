#pragma once

#include "AccountData.h"

namespace Parsers {
bool getDateTime(QJsonValue value, QDateTime& out);
bool getString(QJsonValue value, QString& out);
bool getNumber(QJsonValue value, double& out);
bool getNumber(QJsonValue value, int64_t& out);
bool getBool(QJsonValue value, bool& out);

bool parseMinecraftProfileMojang(QByteArray& data, MinecraftProfile& output);

//! OAuth2 token endpoint response (RFC 6749 section 5.1 / 5.2)
struct OAuthTokenResponse {
    QString accessToken;
    QString refreshToken;
    int expiresIn = 0;

    QString error;
    QString errorDescription;
};
OAuthTokenResponse parseOAuthTokenResponse(const QByteArray& data);

//! Ely.by /api/account/v1/info response
bool parseElyByAccountInfo(const QByteArray& data, MinecraftProfile& output);
}  // namespace Parsers
