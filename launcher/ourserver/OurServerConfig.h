// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QSet>
#include <QString>
#include <QUrl>

/**
 * The single configuration point of "Our Server".
 *
 * - manifest URL: Settings > APIs, otherwise Launcher_OUR_SERVER_MANIFEST_URL (CMake)
 * - server address and port: Settings > APIs, otherwise the "server" object of the manifest
 * - Minecraft version, loader and mods: the manifest only
 */
namespace OurServer {

inline constexpr auto MANAGED_PACK_TYPE = "our-server";
inline constexpr auto DEFAULT_INSTANCE_NAME = "Our Server";

QUrl manifestUrl();

//! host or host:port set by the player, empty to use the manifest
QString serverAddressOverride();

//! keys (ManifestMod::key()) of the optional mods the player enabled; optional mods are disabled by default
QSet<QString> enabledOptionalMods();
void setOptionalModEnabled(const QString& key, bool enabled);

}  // namespace OurServer
