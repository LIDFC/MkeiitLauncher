// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPackPlan.h"

#include <QHash>
#include <QSet>

namespace ServerPack {

int Plan::count(ModState state) const
{
    int result = 0;
    for (const auto& mod : mods) {
        if (mod.state == state) {
            result++;
        }
    }
    return result;
}

bool Plan::needsUpdate() const
{
    return count(ModState::Outdated) > 0 || count(ModState::Corrupted) > 0 || !removals.isEmpty() || (downloads.isEmpty() && lockChanged);
}

QList<ResolvedFile> selectTargets(const QList<ResolvedFile>& resolved, const QSet<QString>& enabledOptionalMods)
{
    QList<ResolvedFile> targets;
    for (const auto& file : resolved) {
        if (!file.mod.optional || enabledOptionalMods.contains(file.mod.key())) {
            targets.append(file);
        }
    }
    return targets;
}

Plan buildPlan(const Manifest& manifest, const QList<ResolvedFile>& targets, const Lock& lock, const FileHasher& hashOf)
{
    Plan plan;
    plan.lockChanged = lock.packVersion != manifest.packVersion;

    QHash<QString, LockEntry> lockByKey;
    QHash<QString, LockEntry> lockByFile;
    for (const auto& entry : lock.files) {
        lockByKey.insert(entry.key(), entry);
        lockByFile.insert(entry.fileName.toLower(), entry);
    }

    QSet<QString> targetFiles;
    for (const auto& target : targets) {
        targetFiles.insert(target.fileName.toLower());

        ModStatus status;
        status.target = target;
        const auto lockEntry = lockByKey.constFind(target.mod.key());
        const bool hasLockEntry = lockEntry != lockByKey.constEnd();
        if (hasLockEntry) {
            status.installedVersion = lockEntry->versionNumber;
        }

        const auto hash = hashOf(target.fileName);
        const auto managedFile = lockByFile.constFind(target.fileName.toLower());
        const bool fileIsManaged = managedFile != lockByFile.constEnd();

        if (hash && *hash == target.mod.sha512) {
            status.state = ModState::UpToDate;
            status.installedVersion = target.versionNumber;
            // an identical file of the player is simply taken over
            if (!hasLockEntry || lockEntry->versionId != target.mod.versionId || lockEntry->fileName != target.fileName ||
                lockEntry->sha512 != target.mod.sha512) {
                plan.lockChanged = true;
            }
        } else if (!hash) {
            status.state = hasLockEntry && lockEntry->versionId != target.mod.versionId ? ModState::Outdated : ModState::Missing;
        } else if (fileIsManaged) {
            status.state = *hash == managedFile->sha512 ? ModState::Outdated : ModState::Corrupted;
        } else {
            status.state = ModState::Conflict;
            plan.conflicts.append(target.fileName);
        }

        if (status.state == ModState::Missing || status.state == ModState::Outdated || status.state == ModState::Corrupted) {
            plan.downloads.append(target);
            plan.downloadSize += target.size;
            plan.lockChanged = true;
        }
        plan.mods.append(status);
    }

    for (const auto& entry : lock.files) {
        if (targetFiles.contains(entry.fileName.toLower())) {
            continue;
        }
        plan.lockChanged = true;
        const auto hash = hashOf(entry.fileName);
        if (!hash) {
            continue;
        }
        if (*hash == entry.sha512) {
            plan.removals.append(entry.fileName);
        } else {
            plan.releasedFiles.append(entry.fileName);
        }
    }

    return plan;
}

Lock lockForManifest(const Manifest& manifest, const QList<ResolvedFile>& targets)
{
    Lock lock;
    lock.packVersion = manifest.packVersion;
    for (const auto& target : targets) {
        LockEntry entry;
        entry.name = target.mod.name;
        entry.source = target.mod.source;
        entry.project = target.mod.project;
        entry.versionId = target.mod.versionId;
        entry.versionNumber = target.versionNumber;
        entry.fileName = target.fileName;
        entry.sha512 = target.mod.sha512;
        lock.files.append(entry);
    }
    return lock;
}

}  // namespace ServerPack
