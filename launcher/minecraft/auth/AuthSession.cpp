#include "AuthSession.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

QString AuthSession::serializeUserProperties()
{
    QJsonObject userAttrs;
    /*
    for (auto key : u.properties.keys())
    {
        auto array = QJsonArray::fromStringList(u.properties.values(key));
        userAttrs.insert(key, array);
    }
    */
    QJsonDocument value(userAttrs);
    return value.toJson(QJsonDocument::Compact);
}

bool AuthSession::MakeOffline(QString offline_playername)
{
    session = "-";
    access_token = "0";
    player_name = offline_playername;
    // an unauthenticated session must not be sent to an authentication server
    authlibInjectorApiUrl.clear();
    authlibInjectorJvmArgs.clear();
    return true;
}

void AuthSession::MakeDemo(QString name, QString u)
{
    uuid = u;
    session = "-";
    access_token = "0";
    player_name = name;
    launchMode = LaunchMode::Demo;
    authlibInjectorApiUrl.clear();
    authlibInjectorJvmArgs.clear();
};
