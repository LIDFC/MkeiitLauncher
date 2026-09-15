// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPackLock.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include "FileSystem.h"
#include "ourserver/ServerPackLogging.h"
#include "ourserver/ServerPackManifest.h"

namespace ServerPack {

namespace {
constexpr int LOCK_FORMAT_VERSION = 1;
}

std::expected<Lock, QString> parseLock(const QByteArray& data)
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(QString("server pack lock file is not valid JSON"));
    }
    const auto root = doc.object();
    if (root.value("formatVersion").toInt(-1) != LOCK_FORMAT_VERSION) {
        return std::unexpected(QString("unsupported server pack lock file format"));
    }

    static const QRegularExpression s_sha512(QRegularExpression::anchoredPattern("[0-9a-f]{128}"));

    Lock lock;
    lock.packVersion = root.value("packVersion").toString();
    for (const auto& value : root.value("files").toArray()) {
        const auto obj = value.toObject();
        LockEntry entry;
        entry.name = obj.value("name").toString();
        entry.source = obj.value("source").toString();
        entry.project = obj.value("project").toString();
        entry.versionId = obj.value("versionId").toString();
        entry.versionNumber = obj.value("versionNumber").toString();
        entry.fileName = obj.value("fileName").toString();
        entry.sha512 = obj.value("sha512").toString().toLower();

        if (!isSafeModFileName(entry.fileName) || !s_sha512.match(entry.sha512).hasMatch() || entry.source.isEmpty() ||
            entry.project.isEmpty()) {
            qCWarning(serverPackLogC) << "[ServerPack] Ignoring invalid lock entry" << entry.fileName;
            continue;
        }
        lock.files.append(entry);
    }
    return lock;
}

QByteArray serializeLock(const Lock& lock)
{
    QJsonArray files;
    for (const auto& entry : lock.files) {
        files.append(QJsonObject{ { "name", entry.name },
                                  { "source", entry.source },
                                  { "project", entry.project },
                                  { "versionId", entry.versionId },
                                  { "versionNumber", entry.versionNumber },
                                  { "fileName", entry.fileName },
                                  { "sha512", entry.sha512 } });
    }
    QJsonObject root{ { "formatVersion", LOCK_FORMAT_VERSION }, { "packVersion", lock.packVersion }, { "files", files } };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::expected<Lock, QString> loadLock(const QString& path)
{
    QFile file(path);
    if (!file.exists()) {
        return Lock{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QString("could not read %1: %2").arg(path, file.errorString()));
    }
    return parseLock(file.readAll());
}

std::expected<void, QString> saveLock(const QString& path, const Lock& lock)
{
    if (!FS::ensureFilePathExists(path)) {
        return std::unexpected(QString("could not create the folder for %1").arg(path));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(QString("could not write %1: %2").arg(path, file.errorString()));
    }
    const auto data = serializeLock(lock);
    if (file.write(data) != data.size() || !file.commit()) {
        return std::unexpected(QString("could not write %1: %2").arg(path, file.errorString()));
    }
    return {};
}

}  // namespace ServerPack
