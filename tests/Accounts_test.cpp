#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/AuthSession.h"
#include "minecraft/auth/ElyBy.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/auth/Parsers.h"
#include "minecraft/launch/InjectAuthlib.h"

class AccountsTest : public QObject {
    Q_OBJECT

    static QJsonObject profileJson(const QString& id, const QString& name)
    {
        QJsonObject skin{ { "id", "" }, { "url", "" }, { "variant", "" } };
        return QJsonObject{ { "id", id }, { "name", name }, { "skin", skin }, { "capes", QJsonArray() } };
    }

    static QJsonObject elyByAccountJson()
    {
        QJsonObject token{
            { "token", "ely-access-token" }, { "refresh_token", "ely-refresh-token" }, { "iat", 1700000000 }, { "exp", 1700086400 }
        };
        return QJsonObject{ { "type", "ElyBy" },
                            { "ely-client-id", "test-client" },
                            { "ely", token },
                            { "profile", profileJson("ffc8fdc95824509e8a57c99b940fb996", "ElyPlayer") } };
    }

    static QJsonObject legacyMicrosoftAccountJson()
    {
        QJsonObject msaToken{ { "token", "msa-token" }, { "refresh_token", "msa-refresh-token" } };
        QJsonObject yggToken{ { "token", "minecraft-token" } };
        return QJsonObject{ { "type", "MSA" },
                            { "msa-client-id", "00000000-0000-4000-8000-000000000000" },
                            { "msa", msaToken },
                            { "ygg", yggToken },
                            { "profile", profileJson("069a79f444e94726a5befca90e38aaf5", "Notch") } };
    }

   private slots:
    void test_offlineAccountCreation()
    {
        auto account = MinecraftAccount::createOffline("Notch");
        QVERIFY(account->accountType() == AccountType::Offline);
        QVERIFY(account->isUsable());
        QVERIFY(!account->isOnline());
        QVERIFY(!account->isLegacyMicrosoft());
        QCOMPARE(account->profileName(), QString("Notch"));
        // same as Java's UUID.nameUUIDFromBytes("OfflinePlayer:Notch"), which offline-mode servers use
        QCOMPARE(account->profileId(), QString("b50ad385829d3141a2167e7d7539ba7f"));
        QCOMPARE(account->accessToken(), QString("0"));
    }

    void test_offlineAccountSerialization()
    {
        auto account = MinecraftAccount::createOffline("Steve_1");
        auto json = account->saveToJson();
        QCOMPARE(json.value("type").toString(), QString("Offline"));

        auto loaded = MinecraftAccount::loadFromJsonV3(json);
        QVERIFY(loaded);
        QVERIFY(loaded->accountType() == AccountType::Offline);
        QCOMPARE(loaded->profileName(), account->profileName());
        QCOMPARE(loaded->profileId(), account->profileId());
        QCOMPARE(loaded->accessToken(), QString("0"));
    }

    void test_elyByAccountSerialization()
    {
        auto account = MinecraftAccount::loadFromJsonV3(elyByAccountJson());
        QVERIFY(account);
        QVERIFY(account->accountType() == AccountType::ElyBy);
        QVERIFY(account->isUsable());
        QVERIFY(account->isOnline());
        QVERIFY(!account->isLegacyMicrosoft());
        QCOMPARE(account->profileName(), QString("ElyPlayer"));
        QCOMPARE(account->profileId(), QString("ffc8fdc95824509e8a57c99b940fb996"));
        QCOMPARE(account->accessToken(), QString("ely-access-token"));
        QCOMPARE(account->accountData()->elyToken.refresh_token, QString("ely-refresh-token"));
        QCOMPARE(account->accountData()->elyClientID, QString("test-client"));

        auto json = account->saveToJson();
        QCOMPARE(json.value("type").toString(), QString("ElyBy"));
        QCOMPARE(json.value("ely-client-id").toString(), QString("test-client"));
        QCOMPARE(json.value("ely").toObject().value("token").toString(), QString("ely-access-token"));
        QCOMPARE(json.value("ely").toObject().value("refresh_token").toString(), QString("ely-refresh-token"));
        // the session token is the OAuth2 access token, it is only stored once
        QVERIFY(!json.contains("ygg"));

        auto reloaded = MinecraftAccount::loadFromJsonV3(json);
        QVERIFY(reloaded);
        QVERIFY(reloaded->accountType() == AccountType::ElyBy);
        QCOMPARE(reloaded->accessToken(), QString("ely-access-token"));
        QCOMPARE(reloaded->profileId(), account->profileId());
        QCOMPARE(reloaded->profileName(), account->profileName());
    }

    void test_legacyMicrosoftAccount()
    {
        auto account = MinecraftAccount::loadFromJsonV3(legacyMicrosoftAccountJson());
        // accounts from older versions must still load, so they can be removed
        QVERIFY(account);
        QVERIFY(account->isLegacyMicrosoft());
        QVERIFY(!account->isUsable());
        QVERIFY(!account->isOnline());
        QVERIFY(account->accountState() == AccountState::Disabled);
        // and must never trigger Microsoft authentication
        QVERIFY(!account->shouldRefresh());
        QCOMPARE(account->profileName(), QString("Notch"));
    }

    void test_unknownAccountType()
    {
        QVERIFY(!MinecraftAccount::loadFromJsonV3(QJsonObject{ { "type", "Mojang" } }));
        QVERIFY(!MinecraftAccount::loadFromJsonV3(QJsonObject{}));
    }

    void test_accountUsabilityAndSelection()
    {
        AccountList accounts;
        QVERIFY(!accounts.anyAccountIsUsable());

        accounts.addAccount(MinecraftAccount::loadFromJsonV3(legacyMicrosoftAccountJson()));
        QVERIFY(!accounts.anyAccountIsUsable());

        auto offline = MinecraftAccount::createOffline("Guest");
        accounts.addAccount(offline);
        QVERIFY(accounts.anyAccountIsUsable());
        QCOMPARE(accounts.count(), 2);

        accounts.setDefaultAccount(offline);
        QVERIFY(accounts.defaultAccount() == offline);
        QVERIFY(accounts.getAccountByProfileName("Guest") == offline);
    }

    void test_offlineSession()
    {
        auto account = MinecraftAccount::createOffline("Guest");
        auto session = std::make_shared<AuthSession>();
        account->fillSession(session);
        QCOMPARE(session->player_name, QString("Guest"));
        QCOMPARE(session->uuid, account->profileId());
        QCOMPARE(session->user_type, QString("offline"));
        QVERIFY(session->authlibInjectorApiUrl.isEmpty());
    }

    void test_elyBySession()
    {
        auto account = MinecraftAccount::loadFromJsonV3(elyByAccountJson());
        QVERIFY(account);
        auto session = std::make_shared<AuthSession>();
        account->fillSession(session);
        QCOMPARE(session->access_token, QString("ely-access-token"));
        QCOMPARE(session->player_name, QString("ElyPlayer"));
        QCOMPARE(session->uuid, QString("ffc8fdc95824509e8a57c99b940fb996"));
        QCOMPARE(session->user_type, QString("mojang"));
        QCOMPARE(session->authlibInjectorApiUrl, ElyBy::AUTHLIB_INJECTOR_API_URL);

        // falling back to offline mode must not send the session to the authentication server
        session->MakeOffline("ElyPlayer");
        QCOMPARE(session->access_token, QString("0"));
        QVERIFY(session->authlibInjectorApiUrl.isEmpty());
        QVERIFY(session->authlibInjectorJvmArgs.isEmpty());
    }

    void test_authlibInjectorArguments()
    {
        const QByteArray metadata(R"({"meta":{"serverName":"Ely.by"}})");
        auto args = InjectAuthlib::buildJvmArguments("/data/authlib-injector-1.2.8.jar", ElyBy::AUTHLIB_INJECTOR_API_URL, metadata);
        QCOMPARE(args.size(), 2);
        QCOMPARE(args[0], QString("-javaagent:/data/authlib-injector-1.2.8.jar=https://authserver.ely.by/api/authlib-injector"));
        QCOMPARE(args[1], QString("-Dauthlibinjector.yggdrasil.prefetched=") + QString::fromLatin1(metadata.toBase64()));

        // without prefetched metadata authlib-injector fetches it by itself
        QCOMPARE(InjectAuthlib::buildJvmArguments("a.jar", ElyBy::AUTHLIB_INJECTOR_API_URL, QByteArray()).size(), 1);
    }

    void test_parseElyByAccountInfo()
    {
        MinecraftProfile profile;
        const QByteArray data(
            R"({"id":1,"uuid":"ffc8fdc9-5824-509e-8a57-c99b940fb996","username":"ErickSkrauch","registeredAt":1470566470,"profileLink":"http://ely.by/u1","preferredLanguage":"be"})");
        QVERIFY(Parsers::parseElyByAccountInfo(data, profile));
        QCOMPARE(profile.id, QString("ffc8fdc95824509e8a57c99b940fb996"));
        QCOMPARE(profile.name, QString("ErickSkrauch"));
        QCOMPARE(profile.skin.url, QString("https://skinsystem.ely.by/skins/ErickSkrauch.png"));
        QVERIFY(profile.validity == Validity::Certain);

        MinecraftProfile invalid;
        QVERIFY(!Parsers::parseElyByAccountInfo(QByteArray(R"({"id":1,"username":"NoUuid"})"), invalid));
        QVERIFY(!Parsers::parseElyByAccountInfo(QByteArray(R"({"id":1,"uuid":"not-a-uuid","username":"Bad"})"), invalid));
        QVERIFY(!Parsers::parseElyByAccountInfo(QByteArray("not json"), invalid));
    }

    void test_parseOAuthTokenResponse()
    {
        auto granted = Parsers::parseOAuthTokenResponse(
            QByteArray(R"({"access_token":"access","refresh_token":"refresh","token_type":"Bearer","expires_in":86400})"));
        QCOMPARE(granted.accessToken, QString("access"));
        QCOMPARE(granted.refreshToken, QString("refresh"));
        QCOMPARE(granted.expiresIn, 86400);
        QVERIFY(granted.error.isEmpty());

        auto pending = Parsers::parseOAuthTokenResponse(QByteArray(R"({"error":"authorization_pending"})"));
        QCOMPARE(pending.error, QString("authorization_pending"));
        QVERIFY(pending.accessToken.isEmpty());
    }
};

QTEST_GUILESS_MAIN(AccountsTest)
#include "Accounts_test.moc"
