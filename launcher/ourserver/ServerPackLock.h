// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <expected>

/**
 * Local state of the installed server pack (server-pack.lock.json in the instance folder).
 *
 * Only files listed here are managed by the server pack: they are the only files that may ever be replaced or removed.
 * Everything else in the mods folder belongs to the player.
 */
namespace ServerPack {

inline constexpr auto LOCK_FILE_NAME = "server-pack.lock.json";

struct LockEntry {
    QString name;
    QString source;
    QString project;
    QString versionId;
    QString versionNumber;
    QString fileName;
    QString sha512;

    QString key() const { return source + ':' + project; }
};

struct Lock {
    QString packVersion;
    QList<LockEntry> files;
};

//! Entries with unsafe file names or invalid hashes are dropped, so they are never touched
std::expected<Lock, QString> parseLock(const QByteArray& data);
QByteArray serializeLock(const Lock& lock);

//! A missing lock file is an empty lock
std::expected<Lock, QString> loadLock(const QString& path);
//! Written atomically
std::expected<void, QString> saveLock(const QString& path, const Lock& lock);

}  // namespace ServerPack
