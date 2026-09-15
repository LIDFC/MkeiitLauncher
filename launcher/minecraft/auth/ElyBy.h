// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

/**
 * Ely.by endpoints.
 *
 * OAuth2:          https://docs.ely.by/en/oauth.html
 * Minecraft auth:  https://docs.ely.by/en/minecraft-auth.html
 * authlib-injector https://docs.ely.by/en/authlib-injector.html
 */
namespace ElyBy {
inline const QString OAUTH_DEVICE_CODE_URL = QStringLiteral("https://account.ely.by/api/oauth2/v1/devicecode");
inline const QString OAUTH_TOKEN_URL = QStringLiteral("https://account.ely.by/api/oauth2/v1/token");
inline const QString ACCOUNT_INFO_URL = QStringLiteral("https://account.ely.by/api/account/v1/info");

// minecraft_server_session allows the OAuth2 access token to be used as the game session token
inline const QString OAUTH_SCOPES = QStringLiteral("account_info offline_access minecraft_server_session");

inline const QString SKIN_URL_TEMPLATE = QStringLiteral("https://skinsystem.ely.by/skins/%1.png");

// Yggdrasil API root as understood by authlib-injector
inline const QString AUTHLIB_INJECTOR_API_URL = QStringLiteral("https://authserver.ely.by/api/authlib-injector");
}  // namespace ElyBy
