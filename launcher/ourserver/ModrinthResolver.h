// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <expected>

#include "ourserver/ServerPackManifest.h"

namespace ServerPack {

//! A manifest mod resolved to the exact file that has to be installed
struct ResolvedFile {
    ManifestMod mod;
    QString versionNumber;
    QString fileName;
    QUrl url;
    qint64 size = 0;
};

//! GET /v2/versions?ids=[...] for all Modrinth mods of the manifest, one request for the whole pack
QUrl modrinthVersionsUrl(const QString& apiBase, const QList<ManifestMod>& mods);

/**
 * Resolves the manifest's Modrinth mods from a /v2/versions response.
 *
 * A file is only accepted if it belongs to the pinned project and version, supports the manifest's Minecraft version
 * and loader, has exactly the SHA-512 from the manifest, is downloaded over HTTPS from the trusted download host and
 * has a safe file name.
 */
std::expected<QList<ResolvedFile>, QString> resolveModrinthFiles(const QByteArray& response,
                                                                 const Manifest& manifest,
                                                                 const QString& trustedDownloadHost);

}  // namespace ServerPack
