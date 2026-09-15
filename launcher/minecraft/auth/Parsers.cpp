#include "Parsers.h"
#include "Json.h"
#include "Logging.h"
#include "minecraft/auth/ElyBy.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>

namespace Parsers {

bool getDateTime(QJsonValue value, QDateTime& out)
{
    if (!value.isString()) {
        return false;
    }
    out = QDateTime::fromString(value.toString(), Qt::ISODate);
    return out.isValid();
}

bool getString(QJsonValue value, QString& out)
{
    if (!value.isString()) {
        return false;
    }
    out = value.toString();
    return true;
}

bool getNumber(QJsonValue value, double& out)
{
    if (!value.isDouble()) {
        return false;
    }
    out = value.toDouble();
    return true;
}

bool getNumber(QJsonValue value, int64_t& out)
{
    if (!value.isDouble()) {
        return false;
    }
    out = (int64_t)value.toDouble();
    return true;
}

bool getBool(QJsonValue value, bool& out)
{
    if (!value.isBool()) {
        return false;
    }
    out = value.toBool();
    return true;
}

namespace {
// these skin URLs are for the MHF_Steve and MHF_Alex accounts (made by a Mojang employee)
// they are needed because the session server doesn't return skin urls for default skins
static const QString SKIN_URL_STEVE =
    "https://textures.minecraft.net/texture/1a4af718455d4aab528e7a61f86fa25e6a369d1768dcb13f7df319a713eb810b";
static const QString SKIN_URL_ALEX =
    "https://textures.minecraft.net/texture/83cee5ca6afcdb171285aa00e8049c297b2dbeba0efb8ff970a5677a1b644032";

bool isDefaultModelSteve(QString uuid)
{
    // need to calculate *Java* hashCode of UUID
    // if number is even, skin/model is steve, otherwise it is alex

    // just in case dashes are in the id
    uuid.remove('-');

    if (uuid.size() != 32) {
        return true;
    }

    // qulonglong is guaranteed to be 64 bits
    // we need to use unsigned numbers to guarantee truncation below
    qulonglong most = uuid.left(16).toULongLong(nullptr, 16);
    qulonglong least = uuid.right(16).toULongLong(nullptr, 16);
    qulonglong xored = most ^ least;
    return ((static_cast<quint32>(xored >> 32)) ^ static_cast<quint32>(xored)) % 2 == 0;
}
}  // namespace

/**
Uses session server for skin/cape lookup instead of profile,
because locked Mojang accounts cannot access profile endpoint
(https://api.minecraftservices.com/minecraft/profile/)

ref: https://wiki.vg/Mojang_API#UUID_to_Profile_and_Skin.2FCape

{
    "id": "<profile identifier>",
    "name": "<player name>",
    "properties": [
        {
            "name": "textures",
            "value": "<base64 string>"
        }
    ]
}

decoded base64 "value":
{
    "timestamp": <java time in ms>,
    "profileId": "<profile uuid>",
    "profileName": "<player name>",
    "textures": {
        "SKIN": {
            "url": "<player skin URL>"
        },
        "CAPE": {
            "url": "<player cape URL>"
        }
    }
}
*/

bool parseMinecraftProfileMojang(QByteArray& data, MinecraftProfile& output)
{
    qDebug() << "Parsing Minecraft profile...";
    qCDebug(authCredentials()) << data;

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error) {
        qWarning() << "Failed to parse response as JSON:" << jsonError.errorString();
        return false;
    }

    auto obj = Json::requireObject(doc, "mojang minecraft profile");
    if (!getString(obj.value("id"), output.id)) {
        qWarning() << "Minecraft profile id is not a string";
        return false;
    }

    if (!getString(obj.value("name"), output.name)) {
        qWarning() << "Minecraft profile name is not a string";
        return false;
    }

    auto propsArray = obj.value("properties").toArray();
    QByteArray texturePayload;
    for (auto p : propsArray) {
        auto pObj = p.toObject();
        auto name = pObj.value("name");
        if (!name.isString() || name.toString() != "textures") {
            continue;
        }

        auto value = pObj.value("value");
        if (value.isString()) {
            texturePayload = QByteArray::fromBase64(value.toString().toUtf8(), QByteArray::AbortOnBase64DecodingErrors);
        }

        if (!texturePayload.isEmpty()) {
            break;
        }
    }

    if (texturePayload.isNull()) {
        qWarning() << "No texture payload data";
        return false;
    }

    doc = QJsonDocument::fromJson(texturePayload, &jsonError);
    if (jsonError.error) {
        qWarning() << "Failed to parse response as JSON:" << jsonError.errorString();
        return false;
    }

    obj = Json::requireObject(doc, "session texture payload");
    auto textures = obj.value("textures");
    if (!textures.isObject()) {
        qWarning() << "No textures array in response";
        return false;
    }

    Skin skinOut;
    // fill in default skin info ourselves, as this endpoint doesn't provide it
    bool steve = isDefaultModelSteve(output.id);
    skinOut.variant = steve ? "CLASSIC" : "SLIM";
    skinOut.url = steve ? SKIN_URL_STEVE : SKIN_URL_ALEX;
    // sadly we can't figure this out, but I don't think it really matters...
    skinOut.id = "00000000-0000-0000-0000-000000000000";
    Cape capeOut;
    auto tObj = textures.toObject();
    for (auto idx = tObj.constBegin(); idx != tObj.constEnd(); ++idx) {
        if (idx->isObject()) {
            if (idx.key() == "SKIN") {
                auto skin = idx->toObject();
                if (!getString(skin.value("url"), skinOut.url)) {
                    qWarning() << "Skin url is not a string";
                    return false;
                }
                skinOut.url.replace("http://textures.minecraft.net", "https://textures.minecraft.net");

                auto maybeMeta = skin.find("metadata");
                if (maybeMeta != skin.end() && maybeMeta->isObject()) {
                    auto meta = maybeMeta->toObject();
                    // might not be present
                    getString(meta.value("model"), skinOut.variant);
                }
            } else if (idx.key() == "CAPE") {
                auto cape = idx->toObject();
                if (!getString(cape.value("url"), capeOut.url)) {
                    qWarning() << "Cape url is not a string";
                    return false;
                }
                capeOut.url.replace("http://textures.minecraft.net", "https://textures.minecraft.net");

                // we don't know the cape ID as it is not returned from the session server
                // so just fake it - changing capes is probably locked anyway :(
                capeOut.alias = "cape";
            }
        }
    }

    output.skin = skinOut;
    if (capeOut.alias == "cape") {
        output.capes = QMap<QString, Cape>({ { capeOut.alias, capeOut } });
        output.currentCape = capeOut.alias;
    }

    output.validity = Validity::Certain;
    return true;
}

/*
{
    "access_token": "...",
    "refresh_token": "...",
    "token_type": "Bearer",
    "expires_in": 86400
}
or
{
    "error": "authorization_pending",
    "error_description": "..."
}
*/
OAuthTokenResponse parseOAuthTokenResponse(const QByteArray& data)
{
    OAuthTokenResponse out;
    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse OAuth2 token response:" << jsonError.errorString();
        return out;
    }
    auto obj = doc.object();
    getString(obj.value("access_token"), out.accessToken);
    getString(obj.value("refresh_token"), out.refreshToken);
    out.expiresIn = obj.value("expires_in").toInt();
    getString(obj.value("error"), out.error);
    getString(obj.value("error_description"), out.errorDescription);
    return out;
}

/*
https://docs.ely.by/en/oauth.html#account-info
{
    "id": 1,
    "uuid": "ffc8fdc9-5824-509e-8a57-c99b940fb996",
    "username": "ErickSkrauch",
    "registeredAt": 1470566470,
    "profileLink": "http://ely.by/u1",
    "preferredLanguage": "be"
}
*/
bool parseElyByAccountInfo(const QByteArray& data, MinecraftProfile& output)
{
    qDebug() << "Parsing Ely.by account info...";
    qCDebug(authCredentials()) << data;

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse Ely.by account info as JSON:" << jsonError.errorString();
        return false;
    }
    auto obj = doc.object();

    QString uuidString;
    if (!getString(obj.value("uuid"), uuidString)) {
        qWarning() << "Ely.by account uuid is not a string";
        return false;
    }
    QUuid uuid(uuidString.size() == 32 ? QUuid::fromString(QString("%1-%2-%3-%4-%5")
                                                               .arg(uuidString.mid(0, 8), uuidString.mid(8, 4), uuidString.mid(12, 4),
                                                                    uuidString.mid(16, 4), uuidString.mid(20, 12)))
                                       : QUuid::fromString(uuidString));
    if (uuid.isNull()) {
        qWarning() << "Ely.by account uuid is not valid";
        return false;
    }

    QString username;
    if (!getString(obj.value("username"), username) || username.isEmpty()) {
        qWarning() << "Ely.by account username is not valid";
        return false;
    }

    MinecraftProfile profile;
    profile.id = uuid.toString(QUuid::Id128);
    profile.name = username;
    profile.skin.url = ElyBy::SKIN_URL_TEMPLATE.arg(QString::fromLatin1(QUrl::toPercentEncoding(username)));
    // keep the previously downloaded skin until a new one is fetched
    if (output.name == username) {
        profile.skin.data = output.skin.data;
    }
    profile.validity = Validity::Certain;
    output = profile;
    return true;
}

}  // namespace Parsers
