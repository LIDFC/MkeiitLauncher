// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPackTask.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <utility>

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"
#include "StringUtils.h"
#include "modplatform/helpers/HashUtils.h"
#include "net/ApiRequest.h"
#include "net/ChecksumValidator.h"
#include "ourserver/ServerPackLogging.h"

using namespace ServerPack;

namespace {
const char* stateName(ModState state)
{
    switch (state) {
        case ModState::UpToDate:
            return "up to date";
        case ModState::Missing:
            return "not installed";
        case ModState::Outdated:
            return "update required";
        case ModState::Corrupted:
            return "corrupted, will be downloaded again";
        case ModState::Conflict:
            return "blocked by a file of the player";
    }
    return "unknown";
}
}  // namespace

ServerPackTask::ServerPackTask(QUrl manifestUrl, QString modsDir, QString lockPath, Mode mode)
    : m_manifestUrl(std::move(manifestUrl)), m_modsDir(std::move(modsDir)), m_lockPath(std::move(lockPath)), m_mode(mode)
{}

void ServerPackTask::executeTask()
{
    if (m_mode == Mode::Apply && (m_modsDir.isEmpty() || m_lockPath.isEmpty())) {
        emitFailed(tr("The server pack instance is missing."));
        return;
    }
    if (!isSecureUrl(m_manifestUrl)) {
        emitFailed(tr("The server pack manifest URL must use HTTPS."));
        return;
    }
    fetchManifest();
}

bool ServerPackTask::abort()
{
    if (m_job) {
        m_job->abort();
    }
    emitAborted();
    return true;
}

void ServerPackTask::fetchManifest()
{
    qCInfo(serverPackLogC) << "[ServerPack] Fetching manifest from" << m_manifestUrl.toString();
    setStatus(tr("Checking the server pack..."));

    auto [request, response] = Net::Request::makeByteArray(m_manifestUrl);
    request->enableAutoRetry(true);
    m_request = request;
    m_response = response;

    m_job.reset(new NetJob("ServerPackManifest", APPLICATION->network()));
    m_job->setAskRetry(false);
    m_job->addNetAction(request);
    connect(m_job.get(), &Task::succeeded, this, &ServerPackTask::onManifestDownloaded);
    connect(m_job.get(), &Task::failed, this, [this](const QString& reason) {
        if (!isRunning()) {
            return;
        }
        qCWarning(serverPackLogC) << "[ServerPack] Could not download the manifest:" << reason;
        emitFailed(tr("Could not download the server pack manifest: %1").arg(reason));
    });
    m_job->start();
}

void ServerPackTask::onManifestDownloaded()
{
    if (!isRunning()) {
        return;
    }

    auto parsed = parseManifest(*m_response);
    if (!parsed) {
        qCWarning(serverPackLogC) << "[ServerPack] Invalid manifest, nothing will be changed:" << parsed.error();
        emitFailed(parsed.error());
        return;
    }
    m_manifest = *parsed;
    m_hasManifest = true;
    qCInfo(serverPackLogC) << "[ServerPack] Manifest version:" << m_manifest.packVersion << "- Minecraft" << m_manifest.minecraft << "-"
                           << m_manifest.loaderType << m_manifest.loaderVersion;

    m_lock = {};
    if (!m_lockPath.isEmpty()) {
        if (auto lock = loadLock(m_lockPath)) {
            m_lock = *lock;
        } else {
            // without a valid lock nothing is considered managed, so nothing will be removed or replaced
            qCWarning(serverPackLogC) << "[ServerPack] Ignoring the lock file:" << lock.error();
        }
    }
    qCInfo(serverPackLogC) << "[ServerPack] Installed version:" << (m_lock.packVersion.isEmpty() ? QString("none") : m_lock.packVersion);

    if (m_manifest.mods.isEmpty()) {
        m_targets.clear();
        createPlan();
        return;
    }
    fetchModrinthFiles();
}

void ServerPackTask::fetchModrinthFiles()
{
    qCInfo(serverPackLogC) << "[ServerPack] Resolving" << m_manifest.mods.size() << "mods on Modrinth";

    auto [request, response] = Net::ApiRequest::makeByteArray(modrinthVersionsUrl(BuildConfig.MODRINTH_PROD_URL, m_manifest.mods));
    request->enableAutoRetry(true);
    m_request = request;
    m_response = response;

    m_job.reset(new NetJob("ServerPackModrinth", APPLICATION->network()));
    m_job->setAskRetry(false);
    m_job->addNetAction(request);
    connect(m_job.get(), &Task::succeeded, this, &ServerPackTask::onModrinthResponse);
    connect(m_job.get(), &Task::failed, this, [this](const QString& reason) {
        if (!isRunning()) {
            return;
        }
        qCWarning(serverPackLogC) << "[ServerPack] Modrinth request failed:" << reason;
        emitFailed(tr("Could not reach Modrinth: %1").arg(reason));
    });
    m_job->start();
}

void ServerPackTask::onModrinthResponse()
{
    if (!isRunning()) {
        return;
    }

    auto resolved = resolveModrinthFiles(*m_response, m_manifest, BuildConfig.MODRINTH_DOWNLOAD_HOST);
    if (!resolved) {
        qCWarning(serverPackLogC) << "[ServerPack] Could not resolve the mods, nothing will be changed:" << resolved.error();
        emitFailed(resolved.error());
        return;
    }
    m_targets = *resolved;
    createPlan();
}

FileHasher ServerPackTask::fileHasher() const
{
    const QString modsDir = m_modsDir;
    return [modsDir](const QString& fileName) -> std::optional<QString> {
        if (modsDir.isEmpty()) {
            return std::nullopt;
        }
        const QFileInfo info(QDir(modsDir), fileName);
        if (!info.isFile()) {
            return std::nullopt;
        }
        return Hashing::hash(info.absoluteFilePath(), Hashing::Algorithm::Sha512).toLower();
    };
}

void ServerPackTask::createPlan()
{
    qCInfo(serverPackLogC) << "[ServerPack] Checking" << m_targets.size() << "mods...";
    m_plan = buildPlan(m_manifest, m_targets, m_lock, fileHasher());
    m_hasPlan = true;

    for (const auto& mod : m_plan.mods) {
        qCInfo(serverPackLogC).noquote() << "[ServerPack]" << mod.target.mod.name << mod.target.versionNumber << ":"
                                         << stateName(mod.state);
    }
    for (const auto& fileName : m_plan.removals) {
        qCInfo(serverPackLogC).noquote() << "[ServerPack]" << fileName << ": no longer part of the server pack";
    }

    if (m_mode == Mode::Check) {
        qCInfo(serverPackLogC) << "[ServerPack]" << (m_plan.isUpToDate() ? "Server pack is up to date" : "Server pack needs changes");
        emitSucceeded();
        return;
    }
    applyPlan();
}

QString ServerPackTask::modFilePath(const QString& fileName) const
{
    if (m_modsDir.isEmpty() || !isSafeModFileName(fileName)) {
        return {};
    }
    const QDir dir(m_modsDir);
    const QString path = QDir::cleanPath(dir.absoluteFilePath(fileName));
    if (QDir::cleanPath(QFileInfo(path).absolutePath()) != QDir::cleanPath(dir.absolutePath())) {
        return {};
    }
    return path;
}

void ServerPackTask::applyPlan()
{
    if (!m_plan.conflicts.isEmpty()) {
        qCWarning(serverPackLogC) << "[ServerPack] Files of the player block the installation:" << m_plan.conflicts;
        emitFailed(tr("These files in the mods folder are not part of the server pack and block its installation: %1")
                       .arg(m_plan.conflicts.join(", ")));
        return;
    }
    if (!FS::ensureFolderPathExists(m_modsDir)) {
        emitFailed(tr("Could not create the mods folder."));
        return;
    }
    if (m_plan.downloads.isEmpty()) {
        finishApply();
        return;
    }

    setStatus(tr("Downloading server pack..."));
    m_downloadNames.clear();
    m_downloadSteps.clear();

    m_job.reset(new NetJob("ServerPackDownload", APPLICATION->network()));
    m_job->setAskRetry(false);
    for (const auto& file : std::as_const(m_plan.downloads)) {
        const auto path = modFilePath(file.fileName);
        if (path.isEmpty()) {
            emitFailed(tr("\"%1\" has an unsafe file name.").arg(file.mod.name));
            return;
        }
        qCInfo(serverPackLogC).noquote() << "[ServerPack] Downloading" << file.mod.name << file.versionNumber;
        // the file is written to a temporary file and only moved into place when the SHA-512 matches
        auto request = Net::ApiRequest::makeFile(file.url, path);
        request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha512, file.mod.sha512));
        request->enableAutoRetry(true);
        m_downloadNames.insert(request->getUid(), file.mod.name);
        m_job->addNetAction(request);
    }

    connect(m_job.get(), &Task::stepProgress, this, &ServerPackTask::onDownloadProgress);
    connect(m_job.get(), &Task::succeeded, this, [this] {
        if (!isRunning()) {
            return;
        }
        qCInfo(serverPackLogC) << "[ServerPack] SHA-512 verified for" << m_plan.downloads.size() << "files";
        finishApply();
    });
    connect(m_job.get(), &Task::failed, this, [this](const QString& reason) {
        if (!isRunning()) {
            return;
        }
        qCWarning(serverPackLogC) << "[ServerPack] Download failed, the installed files were kept:" << reason;
        emitFailed(tr("Could not download the server pack: %1").arg(reason));
    });
    m_job->start();
}

void ServerPackTask::onDownloadProgress(const TaskStepProgress& step)
{
    propagateStepProgress(step);
    m_downloadSteps.insert(step.uid, step);

    qint64 downloaded = 0;
    int finished = 0;
    for (const auto& current : std::as_const(m_downloadSteps)) {
        downloaded += qMax<qint64>(0, current.current);
        if (current.state == TaskStepState::Succeeded) {
            finished++;
        }
    }
    if (step.state == TaskStepState::Running && m_downloadNames.contains(step.uid)) {
        setStatus(tr("Downloading %1...").arg(m_downloadNames.value(step.uid)));
    }

    const qint64 total = qMax<qint64>(m_plan.downloadSize, downloaded);
    setDetails(tr("%1 of %n file(s)", "", m_plan.downloads.size()).arg(finished) + " · " +
               QString("%1 / %2").arg(StringUtils::humanReadableFileSize(downloaded), StringUtils::humanReadableFileSize(total)));
    setProgress(downloaded, total);
}

void ServerPackTask::finishApply()
{
    for (const auto& fileName : std::as_const(m_plan.removals)) {
        const auto path = modFilePath(fileName);
        if (path.isEmpty()) {
            continue;
        }
        if (QFile::remove(path)) {
            qCInfo(serverPackLogC).noquote() << "[ServerPack] Removed" << fileName;
        } else {
            qCWarning(serverPackLogC).noquote() << "[ServerPack] Could not remove" << fileName;
        }
    }
    for (const auto& fileName : std::as_const(m_plan.releasedFiles)) {
        qCWarning(serverPackLogC).noquote() << "[ServerPack]" << fileName << "was modified and is no longer managed by the server pack";
    }

    const auto newLock = lockForManifest(m_manifest, m_targets);
    if (auto saved = saveLock(m_lockPath, newLock); !saved) {
        qCWarning(serverPackLogC) << "[ServerPack] Could not save the lock file:" << saved.error();
        emitFailed(tr("Could not save the server pack state: %1").arg(saved.error()));
        return;
    }
    m_lock = newLock;
    m_plan = buildPlan(m_manifest, m_targets, m_lock, fileHasher());

    qCInfo(serverPackLogC) << "[ServerPack] Server pack update completed";
    emitSucceeded();
}
