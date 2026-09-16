// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

#include "ourserver/ModrinthResolver.h"
#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackManifest.h"

namespace ServerPack {

enum class ModState {
    UpToDate,   //!< the file on disk has the expected SHA-512
    Missing,    //!< not installed yet
    Outdated,   //!< a managed older version is installed (or was, before the file got lost)
    Corrupted,  //!< the managed file exists but its content does not match
    Conflict,   //!< a file of the player with the same name and different content is in the way
};

struct ModStatus {
    ResolvedFile target;
    ModState state = ModState::Missing;
    QString installedVersion;
};

struct Plan {
    QList<ModStatus> mods;
    QList<ResolvedFile> downloads;
    //! managed files that are no longer part of the pack and still unmodified
    QStringList removals;
    //! managed files that were modified by the player: they are kept on disk and stop being managed
    QStringList releasedFiles;
    //! files of the player that block the installation
    QStringList conflicts;
    qint64 downloadSize = 0;
    bool lockChanged = false;

    int count(ModState state) const;
    bool isUpToDate() const { return downloads.isEmpty() && removals.isEmpty() && conflicts.isEmpty() && !lockChanged; }
    //! something that is already installed has to change (as opposed to only installing missing mods)
    bool needsUpdate() const;
};

/**
 * Mods that belong on disk: every required mod plus the optional mods the player enabled (keys as ManifestMod::key()).
 * A disabled optional mod is simply not a target, so buildPlan removes it like any mod that left the pack.
 */
QList<ResolvedFile> selectTargets(const QList<ResolvedFile>& resolved, const QSet<QString>& enabledOptionalMods);

//! SHA-512 (lowercase hex) of a file in the mods folder, std::nullopt if there is no such file
using FileHasher = std::function<std::optional<QString>(const QString& fileName)>;

/**
 * Compares the actual files with the manifest. The pack version is not used to decide what has to be downloaded.
 * Files that are not listed in the lock are never scheduled for removal or replacement.
 */
Plan buildPlan(const Manifest& manifest, const QList<ResolvedFile>& targets, const Lock& lock, const FileHasher& hashOf);

//! Lock describing the pack after the plan was applied successfully
Lock lockForManifest(const Manifest& manifest, const QList<ResolvedFile>& targets);

}  // namespace ServerPack
