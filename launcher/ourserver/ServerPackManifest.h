// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <expected>

/**
 * Server pack manifest of "Our Server".
 *
 * The manifest only pins mods (source, project, exact version and SHA-512). File names and download URLs never come
 * from the manifest, they are resolved through the source's API (see ModrinthResolver.h).
 */
namespace ServerPack {

inline constexpr int MANIFEST_FORMAT_VERSION = 1;
inline constexpr int DEFAULT_SERVER_PORT = 25565;
inline constexpr qsizetype MAX_MOD_DESCRIPTION_LENGTH = 200;

struct ManifestMod {
    QString name;
    QString source;         // only "modrinth" is supported for now, "direct" is reserved
    QString project;        // Modrinth project ID
    QString versionId;      // Modrinth version ID, pins the exact version
    QString sha512;         // lowercase hex
    bool optional = false;  // only installed when the player enables it
    QString description;    // shown to the player for optional mods

    //! identifies a mod across manifest versions
    QString key() const { return source + ':' + project; }
};

struct Manifest {
    QString name;
    QString packVersion;
    QString serverAddress;
    int serverPort = DEFAULT_SERVER_PORT;
    int queryPort = 0;  // UDP port of Query (enable-query=true on the server), 0 if it is not enabled
    QString minecraft;
    QString loaderType;
    QString loaderVersion;
    QList<ManifestMod> mods;
};

//! Parses and validates a manifest. Nothing is changed on disk when this fails.
std::expected<Manifest, QString> parseManifest(const QByteArray& data);

//! Prism component UID for a manifest loader type, empty if the loader is not supported
QString loaderComponentUid(const QString& loaderType);

//! Loader name as used by the Modrinth API
QString loaderModrinthName(const QString& loaderType);

//! true if the available pack version is newer than the installed one
bool isNewerPackVersion(const QString& installed, const QString& available);

//! Validates a mod file name coming from an external source: a plain .jar name without directories, traversal or reserved names
bool isSafeModFileName(const QString& fileName);

//! Manifests and downloads are only accepted over HTTPS
bool isSecureUrl(const QUrl& url);

}  // namespace ServerPack
