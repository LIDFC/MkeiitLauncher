#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>
#include <memory>

#include "modplatform/helpers/HashUtils.h"
#include "ourserver/ModrinthResolver.h"
#include "ourserver/OurServerTranslator.h"
#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackManifest.h"
#include "ourserver/ServerPackPlan.h"
#include "ourserver/ServerStatus.h"
#include "ourserver/ServerStatusTask.h"

using namespace ServerPack;

// NOTE: don't use raw string literals in this file, moc cannot parse them (see Accounts_test.cpp)
class ServerPackTest : public QObject {
    Q_OBJECT

    static QString sha512Of(const QByteArray& data)
    {
        return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha512).toHex());
    }

    static QByteArray toJson(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

    static QJsonObject modJson(const QString& name, const QString& project, const QString& versionId, const QString& sha512)
    {
        return QJsonObject{
            { "name", name }, { "source", "modrinth" }, { "project", project }, { "versionId", versionId }, { "sha512", sha512 }
        };
    }

    static QJsonObject manifestJson(const QJsonArray& mods)
    {
        return QJsonObject{ { "formatVersion", 1 },
                            { "name", "Our Server" },
                            { "packVersion", "1.1.0" },
                            { "server", QJsonObject{ { "address", "play.example.com" }, { "port", 25570 } } },
                            { "minecraft", "1.21.11" },
                            { "loader", QJsonObject{ { "type", "fabric" }, { "version", "0.19.5" } } },
                            { "mods", mods } };
    }

    static QByteArray framedPacket(qint32 id, const QByteArray& payload)
    {
        QByteArray body;
        ServerStatus::writeVarInt(body, id);
        body.append(payload);
        QByteArray packet;
        ServerStatus::writeVarInt(packet, static_cast<qint32>(body.size()));
        packet.append(body);
        return packet;
    }

    static QByteArray statusPayload(const QJsonObject& status)
    {
        const auto json = toJson(status);
        QByteArray payload;
        ServerStatus::writeVarInt(payload, static_cast<qint32>(json.size()));
        payload.append(json);
        return payload;
    }

    static QJsonObject statusJson(int online, int max, const QStringList& names)
    {
        QJsonArray sample;
        for (int i = 0; i < names.size(); i++) {
            sample.append(
                QJsonObject{ { "name", names[i] }, { "id", QString("00000000-0000-3000-8000-%1").arg(i + 1, 12, 10, QChar('0')) } });
        }
        return QJsonObject{ { "version", QJsonObject{ { "name", "Paper 1.21.11" }, { "protocol", 774 } } },
                            { "players", QJsonObject{ { "online", online }, { "max", max }, { "sample", sample } } },
                            { "description", QJsonObject{ { "text", "Our Server" } } } };
    }

    static QStringList playerNames(int count)
    {
        QStringList names;
        for (int i = 1; i <= count; i++) {
            names << QString("Player%1").arg(i);
        }
        return names;
    }

    static QByteArray int32Bytes(qint32 value)
    {
        QByteArray data(4, '\0');
        qToBigEndian(value, data.data());
        return data;
    }

    static QByteArray cString(const QByteArray& value) { return value + QByteArray(1, '\0'); }

    static QByteArray queryHandshakeResponse(qint32 sessionId, const QByteArray& token)
    {
        return QByteArray(1, '\x09') + int32Bytes(sessionId) + cString(token);
    }

    static QByteArray queryStatResponse(qint32 sessionId, int online, int max, const QStringList& names)
    {
        QByteArray data = QByteArray(1, '\0') + int32Bytes(sessionId) + QByteArray::fromHex("73706c69746e756d008000");
        const QList<std::pair<QByteArray, QByteArray>> values = { { "hostname", "A Minecraft Server" },
                                                                  { "gametype", "SMP" },
                                                                  { "game_id", "MINECRAFT" },
                                                                  { "version", "1.21.11" },
                                                                  { "plugins", "Paper on 1.21.11" },
                                                                  { "map", "world" },
                                                                  { "numplayers", QByteArray::number(online) },
                                                                  { "maxplayers", QByteArray::number(max) },
                                                                  { "hostport", "25565" },
                                                                  { "hostip", "127.0.0.1" } };
        for (const auto& [key, value] : values) {
            data += cString(key) + cString(value);
        }
        data += QByteArray(1, '\0') + QByteArray::fromHex("01706c617965725f0000");
        for (const auto& name : names) {
            data += cString(name.toUtf8());
        }
        data += QByteArray(1, '\0');
        return data;
    }

    //! a minimal Minecraft server that answers Server List Ping
    static void serveStatus(QTcpServer& server, const QJsonObject& status)
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, status] {
            auto* socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer, status] {
                buffer->append(socket->readAll());
                while (true) {
                    const auto packet = ServerStatus::readPacket(*buffer);
                    if (packet.state != ServerStatus::ReadState::Complete) {
                        return;
                    }
                    buffer->remove(0, packet.size);
                    if (packet.id == 0x00 && packet.payload.isEmpty()) {
                        socket->write(framedPacket(0x00, statusPayload(status)));
                    } else if (packet.id == 0x01) {
                        socket->write(framedPacket(0x01, packet.payload));
                    }
                }
            });
        });
    }

    static QJsonObject versionJson(const QString& id, const QString& project, const QString& fileName, const QString& sha512)
    {
        QJsonObject file{ { "hashes", QJsonObject{ { "sha512", sha512 }, { "sha1", "0000" } } },
                          { "url", "https://cdn.modrinth.com/data/" + project + "/versions/" + id + "/" + fileName },
                          { "filename", fileName },
                          { "primary", true },
                          { "size", 2048 } };
        return QJsonObject{ { "id", id },
                            { "project_id", project },
                            { "version_number", "1.0.0+1.21.11" },
                            { "game_versions", QJsonArray{ "1.21.11" } },
                            { "loaders", QJsonArray{ "fabric", "quilt" } },
                            { "files", QJsonArray{ file } } };
    }

    static Manifest manifest(const QString& packVersion = "1.1.0")
    {
        Manifest result;
        result.packVersion = packVersion;
        result.minecraft = "1.21.11";
        result.loaderType = "fabric";
        result.loaderVersion = "0.19.5";
        return result;
    }

    static ResolvedFile target(const QString& project, const QString& versionId, const QString& fileName, const QString& sha512)
    {
        ResolvedFile file;
        file.mod.name = project;
        file.mod.source = "modrinth";
        file.mod.project = project;
        file.mod.versionId = versionId;
        file.mod.sha512 = sha512;
        file.versionNumber = versionId;
        file.fileName = fileName;
        file.url = QUrl("https://cdn.modrinth.com/data/" + project + "/" + fileName);
        file.size = 100;
        return file;
    }

    static LockEntry lockEntry(const ResolvedFile& file)
    {
        LockEntry entry;
        entry.name = file.mod.name;
        entry.source = file.mod.source;
        entry.project = file.mod.project;
        entry.versionId = file.mod.versionId;
        entry.versionNumber = file.versionNumber;
        entry.fileName = file.fileName;
        entry.sha512 = file.mod.sha512;
        return entry;
    }

    //! fake mods folder: file name -> SHA-512
    static FileHasher hasher(const QHash<QString, QString>& files)
    {
        return [files](const QString& fileName) -> std::optional<QString> {
            const auto it = files.constFind(fileName);
            if (it == files.constEnd()) {
                return std::nullopt;
            }
            return *it;
        };
    }

    const QString shaA = sha512Of("mod a");
    const QString shaA2 = sha512Of("mod a, new version");
    const QString shaB = sha512Of("mod b");
    const QString shaUser = sha512Of("mod of the player");

   private slots:
    void test_parseManifest()
    {
        const auto parsed = parseManifest(toJson(manifestJson(QJsonArray{ modJson("Sodium", "AANobbMI", "Yp8wLY1P", shaA.toUpper()) })));
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error()));
        QCOMPARE(parsed->name, QString("Our Server"));
        QCOMPARE(parsed->packVersion, QString("1.1.0"));
        QCOMPARE(parsed->serverAddress, QString("play.example.com"));
        QCOMPARE(parsed->serverPort, 25570);
        QCOMPARE(parsed->minecraft, QString("1.21.11"));
        QCOMPARE(parsed->loaderType, QString("fabric"));
        QCOMPARE(parsed->loaderVersion, QString("0.19.5"));
        QCOMPARE(parsed->mods.size(), 1);
        QCOMPARE(parsed->mods[0].project, QString("AANobbMI"));
        QCOMPARE(parsed->mods[0].versionId, QString("Yp8wLY1P"));
        // hashes are normalized to lowercase
        QCOMPARE(parsed->mods[0].sha512, shaA);
        QCOMPARE(parsed->mods[0].key(), QString("modrinth:AANobbMI"));
        QCOMPARE(loaderComponentUid(parsed->loaderType), QString("net.fabricmc.fabric-loader"));
    }

    void test_parseManifestWithoutServer()
    {
        auto json = manifestJson(QJsonArray{});
        json.remove("server");
        const auto parsed = parseManifest(toJson(json));
        QVERIFY(parsed.has_value());
        QVERIFY(parsed->serverAddress.isEmpty());
        QCOMPARE(parsed->serverPort, DEFAULT_SERVER_PORT);
        QCOMPARE(parsed->queryPort, 0);
        QVERIFY(parsed->mods.isEmpty());
    }

    void test_parseManifestQueryPort()
    {
        auto json = manifestJson(QJsonArray{});
        QCOMPARE(parseManifest(toJson(json))->queryPort, 0);

        json["server"] = QJsonObject{ { "address", "play.example.com" }, { "port", 25565 }, { "queryPort", 25575 } };
        const auto parsed = parseManifest(toJson(json));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->serverPort, 25565);
        QCOMPARE(parsed->queryPort, 25575);
    }

    void test_invalidManifest_data()
    {
        QTest::addColumn<QByteArray>("data");
        const auto validMod = modJson("Sodium", "AANobbMI", "Yp8wLY1P", shaA);

        QTest::newRow("not json") << QByteArray("this is not json");
        QTest::newRow("array") << QByteArray("[]");

        auto json = manifestJson(QJsonArray{ validMod });
        json["formatVersion"] = 2;
        QTest::newRow("unsupported format") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json.remove("packVersion");
        QTest::newRow("missing pack version") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json["minecraft"] = "1.21/../../x";
        QTest::newRow("invalid minecraft version") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json["loader"] = QJsonObject{ { "type", "rift" }, { "version", "1.0" } };
        QTest::newRow("unsupported loader") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json["server"] = QJsonObject{ { "address", "example.com" }, { "port", 70000 } };
        QTest::newRow("invalid port") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json["server"] = QJsonObject{ { "address", "example.com" }, { "queryPort", "25565" } };
        QTest::newRow("invalid query port") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json["server"] = QJsonObject{ { "address", "https://example.com/" } };
        QTest::newRow("invalid address") << toJson(json);

        json = manifestJson(QJsonArray{ validMod });
        json["mods"] = QJsonObject{};
        QTest::newRow("mods is not an array") << toJson(json);

        QTest::newRow("slug instead of project id")
            << toJson(manifestJson(QJsonArray{ modJson("Fabric API", "fabric-api", "Yp8wLY1P", shaA) }));
        QTest::newRow("short hash") << toJson(manifestJson(QJsonArray{ modJson("Sodium", "AANobbMI", "Yp8wLY1P", "abcdef") }));
        QTest::newRow("missing name") << toJson(manifestJson(QJsonArray{ modJson("", "AANobbMI", "Yp8wLY1P", shaA) }));

        auto directMod = validMod;
        directMod["source"] = "direct";
        QTest::newRow("unsupported source") << toJson(manifestJson(QJsonArray{ directMod }));

        QTest::newRow("duplicate mod") << toJson(manifestJson(QJsonArray{ validMod, modJson("Sodium 2", "AANobbMI", "Ab12Cd34", shaB) }));
    }

    void test_invalidManifest()
    {
        QFETCH(QByteArray, data);
        const auto parsed = parseManifest(data);
        QVERIFY(!parsed.has_value());
        QVERIFY(!parsed.error().isEmpty());
    }

    void test_packVersionComparison()
    {
        QVERIFY(isNewerPackVersion("1.0.0", "1.1.0"));
        QVERIFY(isNewerPackVersion("1.9.0", "1.10.0"));
        QVERIFY(!isNewerPackVersion("1.10.0", "1.9.0"));
        QVERIFY(!isNewerPackVersion("1.1.0", "1.1.0"));
        QVERIFY(isNewerPackVersion("", "1.0.0"));
        QVERIFY(!isNewerPackVersion("1.0.0", ""));
    }

    void test_safeFileNames_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("safe");

        QTest::newRow("modrinth file") << "fabric-api-0.141.6+1.21.11.jar" << true;
        QTest::newRow("parent directory") << "../evil.jar" << false;
        QTest::newRow("nested traversal") << "mods/../../evil.jar" << false;
        QTest::newRow("slash") << "sub/mod.jar" << false;
        QTest::newRow("backslash") << "sub\\mod.jar" << false;
        QTest::newRow("drive") << "C:mod.jar" << false;
        QTest::newRow("hidden") << ".mod.jar" << false;
        QTest::newRow("not a jar") << "mod.exe" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("reserved device name") << "CON.jar" << false;
        QTest::newRow("trailing space") << "mod.jar " << false;
    }

    void test_safeFileNames()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, safe);
        QCOMPARE(isSafeModFileName(fileName), safe);
    }

    void test_secureUrl()
    {
        QVERIFY(isSecureUrl(QUrl("https://raw.githubusercontent.com/LIDFC/MkeiitLauncher/develop/server/manifest.json")));
        QVERIFY(!isSecureUrl(QUrl("http://example.com/manifest.json")));
        QVERIFY(!isSecureUrl(QUrl("file:///C:/manifest.json")));
        QVERIFY(!isSecureUrl(QUrl()));
    }

    void test_modrinthVersionsUrl()
    {
        Manifest m = manifest();
        m.mods.append(target("AANobbMI", "Yp8wLY1P", "sodium.jar", shaA).mod);
        m.mods.append(target("P7dR8mSH", "6qAuTtLR", "fabric-api.jar", shaB).mod);
        const auto url = modrinthVersionsUrl("https://api.modrinth.com/v2", m.mods);
        QCOMPARE(url.host(), QString("api.modrinth.com"));
        QCOMPARE(url.path(), QString("/v2/versions"));
        const auto query = url.query(QUrl::FullyDecoded);
        QVERIFY(query.contains("Yp8wLY1P"));
        QVERIFY(query.contains("6qAuTtLR"));
    }

    void test_resolveModrinthFiles()
    {
        auto m = manifest();
        m.mods.append(target("AANobbMI", "Yp8wLY1P", "sodium.jar", shaA).mod);
        m.mods.append(target("P7dR8mSH", "6qAuTtLR", "fabric-api.jar", shaB).mod);

        const QJsonArray response{ versionJson("6qAuTtLR", "P7dR8mSH", "fabric-api-0.141.6+1.21.11.jar", shaB),
                                   versionJson("Yp8wLY1P", "AANobbMI", "sodium-fabric-0.6.jar", shaA.toUpper()) };
        const auto resolved = resolveModrinthFiles(QJsonDocument(response).toJson(), m, "cdn.modrinth.com");
        QVERIFY2(resolved.has_value(), resolved ? "" : qPrintable(resolved.error()));
        QCOMPARE(resolved->size(), 2);
        // manifest order is kept
        QCOMPARE(resolved->at(0).fileName, QString("sodium-fabric-0.6.jar"));
        QCOMPARE(resolved->at(0).versionNumber, QString("1.0.0+1.21.11"));
        QCOMPARE(resolved->at(0).size, qint64(2048));
        QCOMPARE(resolved->at(1).url.host(), QString("cdn.modrinth.com"));
    }

    void test_rejectModrinthFiles_data()
    {
        QTest::addColumn<QJsonObject>("version");

        QTest::newRow("sha512 does not match") << versionJson("Yp8wLY1P", "AANobbMI", "sodium.jar", shaB);
        QTest::newRow("other project") << versionJson("Yp8wLY1P", "ZZZZZZZZ", "sodium.jar", shaA);
        QTest::newRow("unsafe file name") << versionJson("Yp8wLY1P", "AANobbMI", "../sodium.jar", shaA);

        auto version = versionJson("Yp8wLY1P", "AANobbMI", "sodium.jar", shaA);
        version["game_versions"] = QJsonArray{ "1.20.1" };
        QTest::newRow("other minecraft version") << version;

        version = versionJson("Yp8wLY1P", "AANobbMI", "sodium.jar", shaA);
        version["loaders"] = QJsonArray{ "forge" };
        QTest::newRow("other loader") << version;

        for (const auto& url :
             { QString("http://cdn.modrinth.com/data/AANobbMI/sodium.jar"), QString("https://evil.example.com/sodium.jar") }) {
            version = versionJson("Yp8wLY1P", "AANobbMI", "sodium.jar", shaA);
            auto files = version["files"].toArray();
            auto file = files[0].toObject();
            file["url"] = url;
            files[0] = file;
            version["files"] = files;
            QTest::newRow(qPrintable("untrusted url " + url)) << version;
        }

        QTest::newRow("version not found") << versionJson("Other123", "AANobbMI", "sodium.jar", shaA);
    }

    void test_rejectModrinthFiles()
    {
        QFETCH(QJsonObject, version);
        auto m = manifest();
        m.mods.append(target("AANobbMI", "Yp8wLY1P", "sodium.jar", shaA).mod);

        const auto resolved = resolveModrinthFiles(QJsonDocument(QJsonArray{ version }).toJson(), m, "cdn.modrinth.com");
        QVERIFY(!resolved.has_value());
        QVERIFY(!resolved.error().isEmpty());
    }

    void test_rejectMalformedModrinthResponse()
    {
        auto m = manifest();
        m.mods.append(target("AANobbMI", "Yp8wLY1P", "sodium.jar", shaA).mod);
        QVERIFY(!resolveModrinthFiles("{\"error\":\"not_found\"}", m, "cdn.modrinth.com").has_value());
        QVERIFY(!resolveModrinthFiles("not json", m, "cdn.modrinth.com").has_value());
    }

    void test_planMissingFiles()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        const auto b = target("BBBBBBBB", "b0000001", "b.jar", shaB);
        const auto plan = buildPlan(manifest(), { a, b }, Lock{}, hasher({ { "b.jar", shaB } }));

        QVERIFY(plan.mods[0].state == ModState::Missing);
        QVERIFY(plan.mods[1].state == ModState::UpToDate);
        QCOMPARE(plan.downloads.size(), 1);
        QCOMPARE(plan.downloads[0].fileName, QString("a.jar"));
        QCOMPARE(plan.downloadSize, qint64(100));
        QVERIFY(!plan.isUpToDate());
        QVERIFY(!plan.needsUpdate());
    }

    void test_planUpToDate()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        Lock lock;
        lock.packVersion = "1.1.0";
        lock.files = { lockEntry(a) };

        const auto plan = buildPlan(manifest(), { a }, lock, hasher({ { "a.jar", shaA }, { "user.jar", shaUser } }));
        QVERIFY(plan.mods[0].state == ModState::UpToDate);
        QVERIFY(plan.isUpToDate());
        QVERIFY(plan.downloads.isEmpty());
        QVERIFY(plan.removals.isEmpty());
    }

    void test_planOutdatedFile()
    {
        const auto oldA = target("AAAAAAAA", "a0000001", "a-1.0.jar", shaA);
        const auto newA = target("AAAAAAAA", "a0000002", "a-2.0.jar", shaA2);
        Lock lock;
        lock.packVersion = "1.0.0";
        lock.files = { lockEntry(oldA) };

        const auto plan = buildPlan(manifest(), { newA }, lock, hasher({ { "a-1.0.jar", shaA } }));
        QVERIFY(plan.mods[0].state == ModState::Outdated);
        QCOMPARE(plan.mods[0].installedVersion, QString("a0000001"));
        QCOMPARE(plan.downloads.size(), 1);
        // the old managed file is replaced by the new one
        QCOMPARE(plan.removals, QStringList{ "a-1.0.jar" });
        QVERIFY(plan.needsUpdate());
    }

    void test_planOutdatedFileWithSameName()
    {
        const auto oldA = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        const auto newA = target("AAAAAAAA", "a0000002", "a.jar", shaA2);
        Lock lock;
        lock.files = { lockEntry(oldA) };

        const auto plan = buildPlan(manifest(), { newA }, lock, hasher({ { "a.jar", shaA } }));
        QVERIFY(plan.mods[0].state == ModState::Outdated);
        QCOMPARE(plan.downloads.size(), 1);
        QVERIFY(plan.removals.isEmpty());
    }

    void test_planCorruptedFile()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        Lock lock;
        lock.packVersion = "1.1.0";
        lock.files = { lockEntry(a) };

        const auto plan = buildPlan(manifest(), { a }, lock, hasher({ { "a.jar", sha512Of("truncated download") } }));
        QVERIFY(plan.mods[0].state == ModState::Corrupted);
        QCOMPARE(plan.downloads.size(), 1);
        QVERIFY(plan.conflicts.isEmpty());
        QVERIFY(plan.needsUpdate());
    }

    void test_planPreservesUserMods()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        Lock lock;
        lock.packVersion = "1.1.0";
        lock.files = { lockEntry(a) };

        // mods of the player are neither listed in the manifest nor in the lock
        const auto plan =
            buildPlan(manifest(), { a }, lock, hasher({ { "a.jar", shaA }, { "minimap.jar", shaUser }, { "shaders.jar", shaB } }));
        QVERIFY(plan.removals.isEmpty());
        QVERIFY(plan.releasedFiles.isEmpty());
        QVERIFY(plan.isUpToDate());
    }

    void test_planDoesNotOverwriteUserFile()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        const auto plan = buildPlan(manifest(), { a }, Lock{}, hasher({ { "a.jar", shaUser } }));
        QVERIFY(plan.mods[0].state == ModState::Conflict);
        QCOMPARE(plan.conflicts, QStringList{ "a.jar" });
        QVERIFY(plan.downloads.isEmpty());
        QVERIFY(!plan.isUpToDate());
    }

    void test_planTakesOverIdenticalUserFile()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        const auto plan = buildPlan(manifest(), { a }, Lock{}, hasher({ { "a.jar", shaA } }));
        QVERIFY(plan.mods[0].state == ModState::UpToDate);
        QVERIFY(plan.downloads.isEmpty());
        QVERIFY(plan.lockChanged);
    }

    void test_planRemovesOnlyUnmodifiedManagedFiles()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        const auto removed = target("RRRRRRRR", "r0000001", "removed.jar", shaB);
        const auto edited = target("EEEEEEEE", "e0000001", "edited.jar", shaA2);
        const auto gone = target("GGGGGGGG", "g0000001", "gone.jar", shaUser);
        Lock lock;
        lock.packVersion = "1.1.0";
        lock.files = { lockEntry(a), lockEntry(removed), lockEntry(edited), lockEntry(gone) };

        const auto plan =
            buildPlan(manifest(), { a }, lock,
                      hasher({ { "a.jar", shaA }, { "removed.jar", shaB }, { "edited.jar", sha512Of("changed by the player") } }));
        QCOMPARE(plan.removals, QStringList{ "removed.jar" });
        QCOMPARE(plan.releasedFiles, QStringList{ "edited.jar" });
        QVERIFY(plan.lockChanged);
        QVERIFY(plan.needsUpdate());
    }

    void test_packVersionIsNotTheOnlyUpdateSignal()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        Lock lock;
        lock.packVersion = "1.0.0";
        lock.files = { lockEntry(a) };

        // new pack version but identical files: nothing to download, only the lock is refreshed
        auto plan = buildPlan(manifest("1.1.0"), { a }, lock, hasher({ { "a.jar", shaA } }));
        QVERIFY(plan.downloads.isEmpty());
        QVERIFY(plan.lockChanged);

        // same pack version but a file is missing: it is downloaded anyway
        lock.packVersion = "1.1.0";
        plan = buildPlan(manifest("1.1.0"), { a }, lock, hasher({}));
        QCOMPARE(plan.downloads.size(), 1);
    }

    void test_sha512Verification()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("a.jar");
        const QByteArray content("jar content");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(content);
        }
        const auto expected = sha512Of(content);
        QCOMPARE(Hashing::hash(path, Hashing::Algorithm::Sha512).toLower(), expected);

        FileHasher realHasher = [&dir](const QString& fileName) -> std::optional<QString> {
            const QString filePath = dir.filePath(fileName);
            if (!QFile::exists(filePath)) {
                return std::nullopt;
            }
            return Hashing::hash(filePath, Hashing::Algorithm::Sha512).toLower();
        };

        const auto a = target("AAAAAAAA", "a0000001", "a.jar", expected);
        Lock lock;
        lock.packVersion = "1.1.0";
        lock.files = { lockEntry(a) };
        QVERIFY(buildPlan(manifest(), { a }, lock, realHasher).isUpToDate());

        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("jar cont");
        }
        QVERIFY(buildPlan(manifest(), { a }, lock, realHasher).mods[0].state == ModState::Corrupted);
    }

    void test_lockRoundTrip()
    {
        const auto a = target("AAAAAAAA", "a0000001", "a.jar", shaA);
        const auto lock = lockForManifest(manifest("2.0.0"), { a });
        const auto parsed = parseLock(serializeLock(lock));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->packVersion, QString("2.0.0"));
        QCOMPARE(parsed->files.size(), 1);
        QCOMPARE(parsed->files[0].fileName, QString("a.jar"));
        QCOMPARE(parsed->files[0].sha512, shaA);
        QCOMPARE(parsed->files[0].key(), a.mod.key());
    }

    void test_lockDropsUnsafeEntries()
    {
        QJsonArray files{
            QJsonObject{ { "source", "modrinth" }, { "project", "AAAAAAAA" }, { "fileName", "../../evil.jar" }, { "sha512", shaA } },
            QJsonObject{ { "source", "modrinth" }, { "project", "BBBBBBBB" }, { "fileName", "b.jar" }, { "sha512", "short" } },
            QJsonObject{ { "source", "modrinth" }, { "project", "CCCCCCCC" }, { "fileName", "c.jar" }, { "sha512", shaB } }
        };
        const auto parsed = parseLock(toJson(QJsonObject{ { "formatVersion", 1 }, { "packVersion", "1.0.0" }, { "files", files } }));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->files.size(), 1);
        QCOMPARE(parsed->files[0].fileName, QString("c.jar"));

        QVERIFY(!parseLock("broken").has_value());
    }

    void test_lockFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("server-pack.lock.json");

        const auto missing = loadLock(path);
        QVERIFY(missing.has_value());
        QVERIFY(missing->files.isEmpty());

        const auto lock = lockForManifest(manifest(), { target("AAAAAAAA", "a0000001", "a.jar", shaA) });
        QVERIFY(saveLock(path, lock).has_value());
        const auto loaded = loadLock(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->files.size(), 1);
    }

    void test_russianPluralForms()
    {
        QCOMPARE(OurServerTranslator::russianPluralForm(1), 0);
        QCOMPARE(OurServerTranslator::russianPluralForm(21), 0);
        QCOMPARE(OurServerTranslator::russianPluralForm(2), 1);
        QCOMPARE(OurServerTranslator::russianPluralForm(24), 1);
        QCOMPARE(OurServerTranslator::russianPluralForm(5), 2);
        QCOMPARE(OurServerTranslator::russianPluralForm(11), 2);
        QCOMPARE(OurServerTranslator::russianPluralForm(12), 2);
        QCOMPARE(OurServerTranslator::russianPluralForm(111), 2);
        QCOMPARE(OurServerTranslator::russianPluralForm(0), 2);
    }

    void test_russianTranslator()
    {
        QVERIFY(OurServerTranslator::supportsLanguage("ru"));
        QVERIFY(OurServerTranslator::supportsLanguage("ru_RU"));
        QVERIFY(!OurServerTranslator::supportsLanguage("en_US"));

        OurServerTranslator translator;
        const auto title = translator.translate("OurServerPage", "Our Server", nullptr, -1);
        QVERIFY(!title.isEmpty());
        QVERIFY(title != "Our Server");

        const auto files = translator.translate("OurServerPage", "%n file(s)", nullptr, 5);
        QVERIFY(files.contains("%n"));

        const auto morePlayers = translator.translate("OurServerPage", "…and %n more", nullptr, 3);
        QVERIFY(morePlayers.contains("%n"));
        QVERIFY(!translator.translate("ServerStatusTask", "The server did not respond in time.", nullptr, -1).isEmpty());

        // strings of the rest of the launcher are left to the regular translations
        QVERIFY(translator.translate("MainWindow", "Add Instanc&e...", nullptr, -1).isEmpty());
    }

    // server status

    void test_varInt_data()
    {
        QTest::addColumn<qint32>("value");
        QTest::addColumn<QByteArray>("encoded");
        QTest::newRow("0") << 0 << QByteArray::fromHex("00");
        QTest::newRow("1") << 1 << QByteArray::fromHex("01");
        QTest::newRow("127") << 127 << QByteArray::fromHex("7f");
        QTest::newRow("128") << 128 << QByteArray::fromHex("8001");
        QTest::newRow("255") << 255 << QByteArray::fromHex("ff01");
        QTest::newRow("25565") << 25565 << QByteArray::fromHex("ddc701");
        QTest::newRow("2097151") << 2097151 << QByteArray::fromHex("ffff7f");
        QTest::newRow("max") << 2147483647 << QByteArray::fromHex("ffffffff07");
        QTest::newRow("-1") << -1 << QByteArray::fromHex("ffffffff0f");
    }

    void test_varInt()
    {
        QFETCH(qint32, value);
        QFETCH(QByteArray, encoded);

        QByteArray data;
        ServerStatus::writeVarInt(data, value);
        QCOMPARE(data, encoded);

        qsizetype offset = 0;
        qint32 decoded = 0;
        QVERIFY(ServerStatus::readVarInt(data, offset, decoded) == ServerStatus::ReadState::Complete);
        QCOMPARE(decoded, value);
        QCOMPARE(offset, data.size());
    }

    void test_readVarIntLimits()
    {
        qsizetype offset = 0;
        qint32 value = 0;
        QVERIFY(ServerStatus::readVarInt(QByteArray::fromHex("ff"), offset, value) == ServerStatus::ReadState::Incomplete);
        QCOMPARE(offset, 0);
        QVERIFY(ServerStatus::readVarInt(QByteArray::fromHex("ffffffffff01"), offset, value) == ServerStatus::ReadState::Invalid);
    }

    void test_readPacket()
    {
        const auto packet = framedPacket(0x01, QByteArray::fromHex("0102030405060708"));
        const QByteArray buffer = packet + QByteArray::fromHex("aabb");
        const auto read = ServerStatus::readPacket(buffer);
        QVERIFY(read.state == ServerStatus::ReadState::Complete);
        QCOMPARE(read.id, 0x01);
        QCOMPARE(read.payload, QByteArray::fromHex("0102030405060708"));
        QCOMPARE(read.size, packet.size());

        QVERIFY(ServerStatus::readPacket(packet.left(packet.size() - 1)).state == ServerStatus::ReadState::Incomplete);
        QVERIFY(ServerStatus::readPacket(QByteArray()).state == ServerStatus::ReadState::Incomplete);
        QVERIFY(ServerStatus::readPacket(QByteArray::fromHex("00")).state == ServerStatus::ReadState::Invalid);

        QByteArray oversized;
        ServerStatus::writeVarInt(oversized, static_cast<qint32>(ServerStatus::MAX_PACKET_LENGTH + 1));
        QVERIFY(ServerStatus::readPacket(oversized).state == ServerStatus::ReadState::Invalid);
    }

    void test_statusPackets()
    {
        const auto handshake = ServerStatus::handshakePacket("mc.example.com", 25565);
        const auto read = ServerStatus::readPacket(handshake);
        QVERIFY(read.state == ServerStatus::ReadState::Complete);
        QCOMPARE(read.size, handshake.size());
        QCOMPARE(read.id, 0x00);
        // protocol version -1, host, port 25565, next state "status"
        const QByteArray expectedPayload =
            QByteArray::fromHex("ffffffff0f0e") + QByteArray("mc.example.com") + QByteArray::fromHex("63dd01");
        QCOMPARE(read.payload, expectedPayload);

        QCOMPARE(ServerStatus::statusRequestPacket(), QByteArray::fromHex("0100"));
        QCOMPARE(ServerStatus::pingRequestPacket(1), QByteArray::fromHex("09010000000000000001"));

        QVERIFY(ServerStatus::parsePongPayload(QByteArray::fromHex("000000000000002a")) == qint64(42));
        QVERIFY(!ServerStatus::parsePongPayload(QByteArray::fromHex("0000002a")).has_value());
    }

    void test_parseStatus()
    {
        auto status = statusJson(3, 20, { "Alice", "Bob" });
        auto players = status["players"].toObject();
        auto sample = players["sample"].toArray();
        // players hiding themselves, invalid names and duplicates are not listed
        sample.append(QJsonObject{ { "name", "Anonymous Player" }, { "id", "00000000-0000-0000-0000-000000000000" } });
        sample.append(QJsonObject{ { "name", "Hello World" }, { "id", "00000000-0000-3000-8000-000000000009" } });
        sample.append(QJsonObject{ { "name", "Alice" }, { "id", "00000000-0000-3000-8000-000000000001" } });
        players["sample"] = sample;
        status["players"] = players;

        const auto parsed = ServerStatus::parseStatusResponse(statusPayload(status));
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error()));
        QCOMPARE(parsed->online, 3);
        QCOMPARE(parsed->max, 20);
        QCOMPARE(parsed->names, QStringList({ "Alice", "Bob" }));
        QCOMPARE(parsed->unlistedCount(), 1);
    }

    void test_parseStatusPartialPlayerList()
    {
        // vanilla and Paper only send 12 names
        const auto parsed = ServerStatus::parseStatusJson(statusJson(30, 50, playerNames(12)));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->names.size(), 12);
        QCOMPARE(parsed->unlistedCount(), 18);

        const auto empty = ServerStatus::parseStatusJson(statusJson(0, 20, {}));
        QVERIFY(empty.has_value());
        QCOMPARE(empty->online, 0);
        QCOMPARE(empty->unlistedCount(), 0);
    }

    void test_parseStatusWithoutPlayers()
    {
        auto status = statusJson(1, 1, {});
        status.remove("players");
        const auto parsed = ServerStatus::parseStatusJson(status);
        QVERIFY(parsed.has_value());
        // unknown, not zero
        QCOMPARE(parsed->online, -1);
        QCOMPARE(parsed->max, -1);
        QCOMPARE(parsed->unlistedCount(), 0);
    }

    void test_invalidStatus_data()
    {
        QTest::addColumn<QByteArray>("payload");

        auto status = statusJson(1, 20, {});
        status["players"] = "many";
        QTest::newRow("players is not an object") << statusPayload(status);

        status["players"] = QJsonObject{ { "online", "3" }, { "max", 20 } };
        QTest::newRow("online is a string") << statusPayload(status);

        status["players"] = QJsonObject{ { "online", -1 }, { "max", 20 } };
        QTest::newRow("negative online") << statusPayload(status);

        status["players"] = QJsonObject{ { "online", 2.5 }, { "max", 20 } };
        QTest::newRow("fractional online") << statusPayload(status);

        status["players"] = QJsonObject{ { "online", 2 } };
        QTest::newRow("missing max") << statusPayload(status);

        QByteArray notJson;
        ServerStatus::writeVarInt(notJson, 5);
        notJson.append("hello");
        QTest::newRow("not json") << notJson;

        QTest::newRow("truncated") << statusPayload(statusJson(1, 20, {})).left(10);
        QTest::newRow("empty") << QByteArray();
    }

    void test_invalidStatus()
    {
        QFETCH(QByteArray, payload);
        const auto parsed = ServerStatus::parseStatusResponse(payload);
        QVERIFY(!parsed.has_value());
        QVERIFY(!parsed.error().isEmpty());
    }

    void test_playerNames()
    {
        QVERIFY(ServerStatus::isValidPlayerName("Steve"));
        QVERIFY(ServerStatus::isValidPlayerName("a_b-c.1"));
        QVERIFY(ServerStatus::isValidPlayerName("SixteenCharsName"));
        QVERIFY(!ServerStatus::isValidPlayerName("SeventeenCharName"));
        QVERIFY(!ServerStatus::isValidPlayerName(""));
        QVERIFY(!ServerStatus::isValidPlayerName("Two Words"));
        QVERIFY(!ServerStatus::isValidPlayerName(QString::fromUtf8("§cRed")));
    }

    void test_queryHandshake()
    {
        const qint32 sessionId = ServerStatus::querySessionId(0xFFFFFFFFU);
        QCOMPARE(sessionId, 0x0F0F0F0F);
        QCOMPARE(ServerStatus::queryHandshakeRequest(sessionId), QByteArray::fromHex("fefd090f0f0f0f"));

        auto token = ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, "9513307"), sessionId);
        QVERIFY(token.has_value());
        QCOMPARE(*token, 9513307);

        token = ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, "-1234"), sessionId);
        QVERIFY(token.has_value());
        QCOMPARE(*token, -1234);

        // tokens above the signed range are sent as their 32 bit representation
        token = ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, "4294967295"), sessionId);
        QVERIFY(token.has_value());
        QCOMPARE(*token, -1);

        QVERIFY(!ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(1, "9513307"), sessionId).has_value());
        QVERIFY(!ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, ""), sessionId).has_value());
        QVERIFY(!ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, "token"), sessionId).has_value());
        QVERIFY(!ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, "99999999999"), sessionId).has_value());
        QVERIFY(!ServerStatus::parseQueryHandshakeResponse(queryHandshakeResponse(sessionId, "9513307").chopped(1), sessionId).has_value());
    }

    void test_queryFullStat()
    {
        const qint32 sessionId = 0x01020304;
        const auto request = ServerStatus::queryFullStatRequest(sessionId, 9513307);
        const QByteArray expectedRequest = QByteArray::fromHex("fefd0001020304") + int32Bytes(9513307) + QByteArray::fromHex("00000000");
        QCOMPARE(request, expectedRequest);

        const auto parsed =
            ServerStatus::parseQueryFullStatResponse(queryStatResponse(sessionId, 3, 20, { "Alice", "Bob", "Carol" }), sessionId);
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error()));
        QCOMPARE(parsed->online, 3);
        QCOMPARE(parsed->max, 20);
        QCOMPARE(parsed->names, QStringList({ "Alice", "Bob", "Carol" }));

        const auto empty = ServerStatus::parseQueryFullStatResponse(queryStatResponse(sessionId, 0, 20, {}), sessionId);
        QVERIFY(empty.has_value());
        QVERIFY(empty->names.isEmpty());
    }

    void test_invalidQueryFullStat()
    {
        const qint32 sessionId = 0x01020304;
        const auto valid = queryStatResponse(sessionId, 2, 20, { "Alice", "Bob" });

        QVERIFY(!ServerStatus::parseQueryFullStatResponse(valid, 0x05060708).has_value());
        QVERIFY(!ServerStatus::parseQueryFullStatResponse(valid.left(40), sessionId).has_value());
        QVERIFY(!ServerStatus::parseQueryFullStatResponse(QByteArray(), sessionId).has_value());

        auto wrongHeader = valid;
        wrongHeader[5] = 'S';
        QVERIFY(!ServerStatus::parseQueryFullStatResponse(wrongHeader, sessionId).has_value());

        auto withoutCount = valid;
        withoutCount.replace("numplayers", "numplayerz");
        QVERIFY(!ServerStatus::parseQueryFullStatResponse(withoutCount, sessionId).has_value());
    }

    void test_mergePlayers()
    {
        ServerStatus::Players status;
        status.online = 14;
        status.max = 20;
        status.names = playerNames(12);
        QCOMPARE(status.unlistedCount(), 2);

        QCOMPARE(ServerStatus::mergePlayers(status, std::nullopt).names, status.names);

        ServerStatus::Players query;
        query.online = 14;
        query.max = 20;
        query.names = playerNames(14);
        const auto merged = ServerStatus::mergePlayers(status, query);
        QCOMPARE(merged.online, 14);
        QCOMPARE(merged.max, 20);
        QCOMPARE(merged.names.size(), 14);
        QCOMPARE(merged.unlistedCount(), 0);
    }

    void test_statusTaskWithQuery()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        serveStatus(server, statusJson(14, 20, playerNames(12)));

        // a minimal Query responder
        QUdpSocket query;
        QVERIFY(query.bind(QHostAddress::LocalHost, 0));
        connect(&query, &QUdpSocket::readyRead, &query, [&query] {
            while (query.hasPendingDatagrams()) {
                const auto datagram = query.receiveDatagram();
                const auto data = datagram.data();
                if (data.size() < 7 || !data.startsWith(QByteArray::fromHex("fefd"))) {
                    continue;
                }
                const auto sessionId = qFromBigEndian<qint32>(data.constData() + 3);
                if (data.at(2) == '\x09') {
                    query.writeDatagram(datagram.makeReply(queryHandshakeResponse(sessionId, "9513307")));
                } else if (data.size() == 15 && qFromBigEndian<qint32>(data.constData() + 7) == 9513307) {
                    query.writeDatagram(datagram.makeReply(queryStatResponse(sessionId, 14, 20, playerNames(14))));
                }
            }
        });

        ServerStatusTask task("127.0.0.1", server.serverPort(), query.localPort());
        QSignalSpy finished(&task, &Task::finished);
        task.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);

        QVERIFY(task.wasSuccessful());
        const auto& result = task.result();
        QVERIFY2(result.online, qPrintable(result.error));
        QCOMPARE(result.players.online, 14);
        QCOMPARE(result.players.max, 20);
        QVERIFY(result.latencyMs >= 0);
        QVERIFY2(result.fullPlayerList, qPrintable(result.queryError));
        QCOMPARE(result.players.names, playerNames(14));
        QCOMPARE(result.players.unlistedCount(), 0);
        QVERIFY(result.checkedAt.isValid());
    }

    void test_statusTaskWithoutQueryResponse()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        serveStatus(server, statusJson(14, 20, playerNames(12)));

        // Query is configured, but nothing answers
        QUdpSocket silent;
        QVERIFY(silent.bind(QHostAddress::LocalHost, 0));

        ServerStatusTask task("127.0.0.1", server.serverPort(), silent.localPort());
        QSignalSpy finished(&task, &Task::finished);
        task.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);

        const auto& result = task.result();
        QVERIFY(result.online);
        QVERIFY(!result.fullPlayerList);
        QVERIFY(!result.queryError.isEmpty());
        // the partial list of Server List Ping is kept
        QCOMPARE(result.players.names.size(), 12);
        QCOMPARE(result.players.unlistedCount(), 2);
    }

    void test_statusTaskUnavailableServer()
    {
        quint16 port = 0;
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            port = server.serverPort();
        }

        ServerStatusTask task("127.0.0.1", port, 0);
        QSignalSpy finished(&task, &Task::finished);
        task.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);

        QVERIFY(task.wasSuccessful());
        const auto& result = task.result();
        QVERIFY(!result.online);
        QVERIFY(!result.error.isEmpty());
        // nothing is made up for an unavailable server
        QCOMPARE(result.players.online, -1);
        QCOMPARE(result.players.max, -1);
        QVERIFY(result.players.names.isEmpty());
        QCOMPARE(result.latencyMs, qint64(-1));
        QVERIFY(result.checkedAt.isValid());
    }
};

QTEST_GUILESS_MAIN(ServerPackTest)
#include "ServerPack_test.moc"
