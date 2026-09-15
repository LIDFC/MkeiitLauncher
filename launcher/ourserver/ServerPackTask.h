// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QHash>
#include <QUrl>
#include <QUuid>

#include "net/NetJob.h"
#include "ourserver/ModrinthResolver.h"
#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackManifest.h"
#include "ourserver/ServerPackPlan.h"
#include "tasks/Task.h"

/**
 * Checks (and in Apply mode installs) the server pack of "Our Server".
 *
 * 1. fetches the manifest over HTTPS and validates it
 * 2. resolves all mods with one Modrinth API request
 * 3. compares the actual files with the manifest and the lock file
 * 4. Apply: downloads missing/outdated/corrupted files with Prism's downloader (retries, cancellation, temporary file +
 *    rename, SHA-512 validation), then removes unmodified managed files that left the pack and writes the lock file
 *
 * Nothing on disk is changed before the manifest and the Modrinth response are fully validated.
 */
class ServerPackTask : public Task {
    Q_OBJECT
   public:
    enum class Mode { Check, Apply };
    using Ptr = shared_qobject_ptr<ServerPackTask>;

    //! modsDir and lockPath may be empty in Check mode when the server instance does not exist yet
    ServerPackTask(QUrl manifestUrl, QString modsDir, QString lockPath, Mode mode);
    ~ServerPackTask() override = default;

    Mode mode() const { return m_mode; }

    bool hasManifest() const { return m_hasManifest; }
    const ServerPack::Manifest& manifest() const { return m_manifest; }

    bool hasPlan() const { return m_hasPlan; }
    const ServerPack::Plan& plan() const { return m_plan; }
    const ServerPack::Lock& installedLock() const { return m_lock; }

    bool canAbort() const override { return true; }

   public slots:
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void fetchManifest();
    void onManifestDownloaded();
    void fetchModrinthFiles();
    void onModrinthResponse();
    void createPlan();
    void applyPlan();
    void onDownloadProgress(const TaskStepProgress& step);
    void finishApply();

    ServerPack::FileHasher fileHasher() const;
    //! absolute path inside the mods folder, empty if the file name is not safe
    QString modFilePath(const QString& fileName) const;

    QUrl m_manifestUrl;
    QString m_modsDir;
    QString m_lockPath;
    Mode m_mode;

    bool m_hasManifest = false;
    bool m_hasPlan = false;
    ServerPack::Manifest m_manifest;
    ServerPack::Lock m_lock;
    QList<ServerPack::ResolvedFile> m_targets;
    ServerPack::Plan m_plan;

    NetJob::Ptr m_job;
    Net::Request::Ptr m_request;
    QByteArray* m_response = nullptr;

    QHash<QUuid, QString> m_downloadNames;
    QHash<QUuid, TaskStepProgress> m_downloadSteps;
};
