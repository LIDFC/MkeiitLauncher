// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPackManifest.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <optional>

#include "Version.h"

namespace ServerPack {

namespace {
const QRegularExpression& modrinthIdPattern()
{
    static const QRegularExpression s_pattern(QRegularExpression::anchoredPattern("[A-Za-z0-9]{8}"));
    return s_pattern;
}

const QRegularExpression& versionPattern()
{
    static const QRegularExpression s_pattern(QRegularExpression::anchoredPattern("[0-9A-Za-z][0-9A-Za-z.+_\\-]{0,63}"));
    return s_pattern;
}

const QRegularExpression& sha512Pattern()
{
    static const QRegularExpression s_pattern(QRegularExpression::anchoredPattern("[0-9a-f]{128}"));
    return s_pattern;
}

const QRegularExpression& hostPattern()
{
    static const QRegularExpression s_pattern(QRegularExpression::anchoredPattern(
        "[A-Za-z0-9]([A-Za-z0-9\\-]{0,61}[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9\\-]{0,61}[A-Za-z0-9])?)*"));
    return s_pattern;
}

bool matches(const QRegularExpression& pattern, const QString& value)
{
    return pattern.match(value).hasMatch();
}

std::optional<int> readPort(const QJsonValue& value)
{
    const double port = value.toDouble(-1);
    if (!value.isDouble() || port < 1 || port > 65535 || port != static_cast<int>(port)) {
        return std::nullopt;
    }
    return static_cast<int>(port);
}
}  // namespace

std::expected<Manifest, QString> parseManifest(const QByteArray& data)
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "The server pack manifest is not valid JSON."));
    }
    const auto root = doc.object();

    if (root.value("formatVersion").toInt(-1) != MANIFEST_FORMAT_VERSION) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "The server pack manifest format version is not supported."));
    }

    Manifest manifest;
    manifest.name = root.value("name").toString().trimmed();

    manifest.packVersion = root.value("packVersion").toString();
    if (!matches(versionPattern(), manifest.packVersion)) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "The server pack manifest has an invalid pack version."));
    }

    manifest.minecraft = root.value("minecraft").toString();
    if (!matches(versionPattern(), manifest.minecraft)) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "The server pack manifest has an invalid Minecraft version."));
    }

    const auto loader = root.value("loader").toObject();
    manifest.loaderType = loader.value("type").toString();
    manifest.loaderVersion = loader.value("version").toString();
    if (loaderComponentUid(manifest.loaderType).isEmpty() || !matches(versionPattern(), manifest.loaderVersion)) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "The server pack manifest has an unsupported or invalid loader."));
    }

    if (const auto serverValue = root.value("server"); !serverValue.isUndefined()) {
        const auto server = serverValue.toObject();
        manifest.serverAddress = server.value("address").toString().trimmed();
        bool validPort = true;
        if (const auto portValue = server.value("port"); !portValue.isUndefined()) {
            const auto port = readPort(portValue);
            validPort = port.has_value();
            manifest.serverPort = port.value_or(DEFAULT_SERVER_PORT);
        }
        if (const auto queryPortValue = server.value("queryPort"); !queryPortValue.isUndefined()) {
            const auto queryPort = readPort(queryPortValue);
            validPort = validPort && queryPort.has_value();
            manifest.queryPort = queryPort.value_or(0);
        }
        if (!serverValue.isObject() || !validPort ||
            (!manifest.serverAddress.isEmpty() && !matches(hostPattern(), manifest.serverAddress))) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The server pack manifest has an invalid server address or port."));
        }
    }

    const auto modsValue = root.value("mods");
    if (!modsValue.isArray()) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "The server pack manifest has an invalid mod list."));
    }

    QSet<QString> keys;
    for (const auto& value : modsValue.toArray()) {
        const auto obj = value.toObject();
        ManifestMod mod;
        mod.name = obj.value("name").toString().trimmed();
        mod.source = obj.value("source").toString();
        mod.project = obj.value("project").toString();
        mod.versionId = obj.value("versionId").toString();
        mod.sha512 = obj.value("sha512").toString().toLower();

        const QString displayName = mod.name.isEmpty() ? mod.project : mod.name;
        if (!value.isObject() || mod.name.isEmpty() || mod.name.size() > 128) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The server pack manifest entry \"%1\" is invalid.").arg(displayName));
        }
        if (mod.source != "modrinth") {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The server pack manifest entry \"%1\" uses an unsupported source.")
                    .arg(displayName));
        }
        if (!matches(modrinthIdPattern(), mod.project) || !matches(modrinthIdPattern(), mod.versionId) ||
            !matches(sha512Pattern(), mod.sha512)) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The server pack manifest entry \"%1\" is invalid.").arg(displayName));
        }
        if (keys.contains(mod.key())) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The server pack manifest lists \"%1\" more than once.").arg(displayName));
        }
        keys.insert(mod.key());
        manifest.mods.append(mod);
    }

    return manifest;
}

QString loaderComponentUid(const QString& loaderType)
{
    if (loaderType == "fabric") {
        return "net.fabricmc.fabric-loader";
    }
    if (loaderType == "quilt") {
        return "org.quiltmc.quilt-loader";
    }
    if (loaderType == "forge") {
        return "net.minecraftforge";
    }
    if (loaderType == "neoforge") {
        return "net.neoforged";
    }
    return {};
}

QString loaderModrinthName(const QString& loaderType)
{
    return loaderComponentUid(loaderType).isEmpty() ? QString() : loaderType;
}

bool isNewerPackVersion(const QString& installed, const QString& available)
{
    if (available.isEmpty()) {
        return false;
    }
    if (installed.isEmpty()) {
        return true;
    }
    return Version(available) > Version(installed);
}

bool isSafeModFileName(const QString& fileName)
{
    if (fileName.isEmpty() || fileName.size() > 255) {
        return false;
    }
    if (fileName.startsWith('.') || fileName.endsWith('.') || fileName.endsWith(' ') || fileName.contains("..")) {
        return false;
    }
    if (!fileName.endsWith(".jar", Qt::CaseInsensitive)) {
        return false;
    }
    static const QString s_forbidden = QStringLiteral("<>:\"/\\|?*");
    for (const QChar c : fileName) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f || s_forbidden.contains(c)) {
            return false;
        }
    }
    static const QRegularExpression s_reserved(QRegularExpression::anchoredPattern("(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\\..*)?"),
                                               QRegularExpression::CaseInsensitiveOption);
    return !s_reserved.match(fileName).hasMatch();
}

bool isSecureUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty();
}

}  // namespace ServerPack
