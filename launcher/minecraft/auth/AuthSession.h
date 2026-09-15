#pragma once

#include <QString>
#include <QStringList>
#include <memory>

#include "LaunchMode.h"

class MinecraftAccount;

struct AuthSession {
    bool MakeOffline(QString offline_playername);
    void MakeDemo(QString name, QString uuid);

    QString serializeUserProperties();

    // combined session ID
    QString session;
    // volatile auth token
    QString access_token;
    // profile name
    QString player_name;
    // profile ID
    QString uuid;
    // 'offline' or 'mojang', depending on account type
    QString user_type;
    // the actual launch mode for this session
    LaunchMode launchMode;

    // authlib-injector API root of the authentication server, empty if the game should not be patched
    QString authlibInjectorApiUrl;
    // JVM arguments prepared by the InjectAuthlib launch step
    QStringList authlibInjectorJvmArgs;
};

using AuthSessionPtr = std::shared_ptr<AuthSession>;
