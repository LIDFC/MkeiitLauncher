#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "modplatform/helpers/HashUtils.h"
#include "ourserver/ModrinthResolver.h"
#include "ourserver/OurServerTranslator.h"
#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackManifest.h"
#include "ourserver/ServerPackPlan.h"

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
        QVERIFY(parsed->mods.isEmpty());
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

        // strings of the rest of the launcher are left to the regular translations
        QVERIFY(translator.translate("MainWindow", "Add Instanc&e...", nullptr, -1).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ServerPackTest)
#include "ServerPack_test.moc"
