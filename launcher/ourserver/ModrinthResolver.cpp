// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthResolver.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrlQuery>

namespace ServerPack {

namespace {
bool arrayContains(const QJsonValue& value, const QString& expected)
{
    for (const auto& item : value.toArray()) {
        if (item.toString() == expected) {
            return true;
        }
    }
    return false;
}
}  // namespace

QUrl modrinthVersionsUrl(const QString& apiBase, const QList<ManifestMod>& mods)
{
    QStringList ids;
    for (const auto& mod : mods) {
        if (mod.source == "modrinth") {
            ids << mod.versionId;
        }
    }
    QUrl url(apiBase + "/versions");
    QUrlQuery query;
    query.addQueryItem("ids", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(ids)).toJson(QJsonDocument::Compact)));
    url.setQuery(query);
    return url;
}

std::expected<QList<ResolvedFile>, QString> resolveModrinthFiles(const QByteArray& response,
                                                                 const Manifest& manifest,
                                                                 const QString& trustedDownloadHost)
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return std::unexpected(QCoreApplication::translate("ServerPack", "Modrinth returned an unexpected response."));
    }

    QHash<QString, QJsonObject> versions;
    for (const auto& value : doc.array()) {
        const auto obj = value.toObject();
        versions.insert(obj.value("id").toString(), obj);
    }

    const auto loader = loaderModrinthName(manifest.loaderType);
    QList<ResolvedFile> resolved;
    QSet<QString> fileNames;

    for (const auto& mod : manifest.mods) {
        if (mod.source != "modrinth") {
            continue;
        }

        const auto versionIt = versions.constFind(mod.versionId);
        if (versionIt == versions.constEnd()) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The version of \"%1\" was not found on Modrinth.").arg(mod.name));
        }
        const auto& version = *versionIt;
        const auto versionNumber = version.value("version_number").toString();

        if (version.value("project_id").toString() != mod.project) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The Modrinth version of \"%1\" belongs to a different project.").arg(mod.name));
        }
        if (!arrayContains(version.value("game_versions"), manifest.minecraft)) {
            return std::unexpected(QCoreApplication::translate("ServerPack", "\"%1\" %2 does not support Minecraft %3.")
                                       .arg(mod.name, versionNumber, manifest.minecraft));
        }
        if (!arrayContains(version.value("loaders"), loader)) {
            return std::unexpected(QCoreApplication::translate("ServerPack", "\"%1\" %2 does not support the %3 loader.")
                                       .arg(mod.name, versionNumber, loader));
        }

        QJsonObject file;
        bool found = false;
        for (const auto& fileValue : version.value("files").toArray()) {
            const auto candidate = fileValue.toObject();
            if (candidate.value("hashes").toObject().value("sha512").toString().toLower() == mod.sha512) {
                file = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "The SHA-512 of \"%1\" does not match any file of its Modrinth version.")
                    .arg(mod.name));
        }

        const QUrl url(file.value("url").toString());
        if (!isSecureUrl(url) || url.host() != trustedDownloadHost) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "\"%1\" is not downloaded from a trusted Modrinth address.").arg(mod.name));
        }

        const auto fileName = file.value("filename").toString();
        if (!isSafeModFileName(fileName)) {
            return std::unexpected(QCoreApplication::translate("ServerPack", "\"%1\" has an unsafe file name.").arg(mod.name));
        }
        if (fileNames.contains(fileName.toLower())) {
            return std::unexpected(
                QCoreApplication::translate("ServerPack", "Several mods of the server pack use the file name \"%1\".").arg(fileName));
        }
        fileNames.insert(fileName.toLower());

        ResolvedFile result;
        result.mod = mod;
        result.versionNumber = versionNumber;
        result.fileName = fileName;
        result.url = url;
        result.size = static_cast<qint64>(file.value("size").toDouble());
        resolved.append(result);
    }

    return resolved;
}

}  // namespace ServerPack
