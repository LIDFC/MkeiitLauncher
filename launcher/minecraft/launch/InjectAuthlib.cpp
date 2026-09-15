// SPDX-License-Identifier: GPL-3.0-only
#include "InjectAuthlib.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "Application.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "net/ChecksumValidator.h"

namespace {
// official download API, see https://github.com/yushijinhun/authlib-injector
const QString LATEST_ARTIFACT_URL = QStringLiteral("https://authlib-injector.yushi.moe/artifact/latest.json");
const QString DOWNLOAD_HOST = QStringLiteral("authlib-injector.yushi.moe");
const QString PREFETCHED_ARGUMENT = QStringLiteral("-Dauthlibinjector.yggdrasil.prefetched=");

QString fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}
}  // namespace

InjectAuthlib::InjectAuthlib(LaunchTask* parent, AuthSessionPtr session) : LaunchStep(parent), m_session(std::move(session)) {}

QStringList InjectAuthlib::buildJvmArguments(const QString& jarPath, const QString& apiUrl, const QByteArray& apiMetadata)
{
    QStringList args{ QString("-javaagent:%1=%2").arg(jarPath, apiUrl) };
    if (!apiMetadata.isEmpty()) {
        args << PREFETCHED_ARGUMENT + QString::fromLatin1(apiMetadata.toBase64());
    }
    return args;
}

QStringList InjectAuthlib::describeJvmArguments(const QStringList& args)
{
    QStringList out;
    out.reserve(args.size());
    for (const auto& arg : args) {
        if (arg.startsWith(PREFETCHED_ARGUMENT)) {
            out << PREFETCHED_ARGUMENT + QString("<%1 characters of prefetched metadata>").arg(arg.size() - PREFETCHED_ARGUMENT.size());
        } else {
            out << arg;
        }
    }
    return out;
}

QString InjectAuthlib::cacheDirectory()
{
    return FS::PathCombine(APPLICATION->dataRoot(), "libraries", "authlib-injector");
}

void InjectAuthlib::executeTask()
{
    if (!m_session || m_session->authlibInjectorApiUrl.isEmpty()) {
        emitSucceeded();
        return;
    }
    if (!FS::ensureFolderPathExists(cacheDirectory())) {
        emitFailed(tr("Could not create the authlib-injector directory."));
        return;
    }
    emit logLine(tr("Preparing authlib-injector for %1").arg(m_session->authlibInjectorApiUrl), MessageLevel::Launcher);
    fetchApiMetadata();
}

void InjectAuthlib::fetchApiMetadata()
{
    auto [request, response] = Net::Request::makeByteArray(QUrl(m_session->authlibInjectorApiUrl));
    request->enableAutoRetry(true);

    m_metadataJob.reset(new NetJob("authlib-injector API metadata", APPLICATION->network()));
    m_metadataJob->setAskRetry(false);
    m_metadataJob->addNetAction(request);
    connect(m_metadataJob.get(), &Task::finished, this, [this, request, response] {
        if (!isRunning()) {
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(*response, &error);
        if (request->error() == QNetworkReply::NoError && error.error == QJsonParseError::NoError && doc.isObject()) {
            m_apiMetadata = *response;
        } else {
            // not fatal, authlib-injector can fetch the metadata by itself
            emit logLine(tr("Could not prefetch the authentication server metadata: %1").arg(request->errorString()),
                         MessageLevel::Warning);
        }
        fetchLatestVersion();
    });
    m_metadataJob->start();
}

void InjectAuthlib::fetchLatestVersion()
{
    auto [request, response] = Net::Request::makeByteArray(QUrl(LATEST_ARTIFACT_URL));
    request->enableAutoRetry(true);

    m_versionJob.reset(new NetJob("authlib-injector version", APPLICATION->network()));
    m_versionJob->setAskRetry(false);
    m_versionJob->addNetAction(request);
    connect(m_versionJob.get(), &Task::finished, this,
            [this, request, response] { onLatestVersion(request->error() == QNetworkReply::NoError ? *response : QByteArray()); });
    m_versionJob->start();
}

void InjectAuthlib::onLatestVersion(const QByteArray& data)
{
    if (!isRunning()) {
        return;
    }

    static const QRegularExpression s_versionRegex(QRegularExpression::anchoredPattern("[0-9A-Za-z.\\-]+"));
    static const QRegularExpression s_sha256Regex(QRegularExpression::anchoredPattern("[0-9a-f]{64}"));

    QJsonParseError error;
    const auto obj = QJsonDocument::fromJson(data, &error).object();
    const auto version = obj.value("version").toString();
    const QUrl downloadUrl(obj.value("download_url").toString());
    const auto sha256 = obj.value("checksums").toObject().value("sha256").toString().toLower();

    const bool valid = !data.isEmpty() && error.error == QJsonParseError::NoError && s_versionRegex.match(version).hasMatch() &&
                       s_sha256Regex.match(sha256).hasMatch() && downloadUrl.scheme() == "https" && downloadUrl.host() == DOWNLOAD_HOST;
    if (!valid) {
        emit logLine(tr("Could not check the latest authlib-injector version."), MessageLevel::Warning);
        useCachedJar();
        return;
    }

    m_jarPath = FS::PathCombine(cacheDirectory(), QString("authlib-injector-%1.jar").arg(version));
    if (QFileInfo::exists(m_jarPath) && fileSha256(m_jarPath) == sha256) {
        finish();
        return;
    }

    emit logLine(tr("Downloading authlib-injector %1").arg(version), MessageLevel::Launcher);
    auto request = Net::Request::makeFile(downloadUrl, m_jarPath);
    request->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, sha256));

    m_downloadJob.reset(new NetJob("authlib-injector download", APPLICATION->network()));
    m_downloadJob->setAskRetry(false);
    m_downloadJob->addNetAction(request);
    connect(m_downloadJob.get(), &Task::succeeded, this, &InjectAuthlib::finish);
    connect(m_downloadJob.get(), &Task::failed, this, [this](const QString& reason) {
        if (!isRunning()) {
            return;
        }
        emit logLine(tr("Could not download authlib-injector: %1").arg(reason), MessageLevel::Warning);
        useCachedJar();
    });
    m_downloadJob->start();
}

void InjectAuthlib::useCachedJar()
{
    if (!isRunning()) {
        return;
    }
    // jars in the cache directory were verified when they were downloaded
    QDir dir(cacheDirectory());
    const auto jars = dir.entryInfoList({ "authlib-injector-*.jar" }, QDir::Files, QDir::Time);
    if (jars.isEmpty()) {
        emit logLine(tr("authlib-injector is not available, the game cannot authenticate with %1.").arg(m_session->authlibInjectorApiUrl),
                     MessageLevel::Fatal);
        emitFailed(tr("Could not download authlib-injector"));
        return;
    }
    m_jarPath = jars.first().absoluteFilePath();
    emit logLine(tr("Using previously downloaded %1").arg(jars.first().fileName()), MessageLevel::Warning);
    finish();
}

void InjectAuthlib::finish()
{
    if (!isRunning()) {
        return;
    }
    m_session->authlibInjectorJvmArgs = buildJvmArguments(m_jarPath, m_session->authlibInjectorApiUrl, m_apiMetadata);
    emit logLine(tr("Using authlib-injector: %1").arg(m_jarPath), MessageLevel::Launcher);
    emitSucceeded();
}

bool InjectAuthlib::abort()
{
    for (const auto& job : { m_metadataJob, m_versionJob, m_downloadJob }) {
        if (job) {
            job->abort();
        }
    }
    emitAborted();
    return true;
}
