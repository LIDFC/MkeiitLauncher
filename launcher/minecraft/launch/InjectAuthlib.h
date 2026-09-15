// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QStringList>

#include "launch/LaunchStep.h"
#include "minecraft/auth/AuthSession.h"
#include "net/NetJob.h"

/**
 * Prepares authlib-injector (https://github.com/yushijinhun/authlib-injector) so the game authenticates against the
 * session's authentication server instead of Mojang's.
 *
 * The jar is downloaded from the official download API and verified with the published SHA-256 checksum.
 * The resulting JVM arguments are stored in AuthSession::authlibInjectorJvmArgs.
 */
class InjectAuthlib : public LaunchStep {
    Q_OBJECT
   public:
    explicit InjectAuthlib(LaunchTask* parent, AuthSessionPtr session);
    ~InjectAuthlib() override = default;

    void executeTask() override;
    bool abort() override;
    bool canAbort() const override { return true; }

    //! JVM arguments as described in the authlib-injector launcher technical specification
    static QStringList buildJvmArguments(const QString& jarPath, const QString& apiUrl, const QByteArray& apiMetadata);

    //! Copy of JVM arguments for logging, with the long prefetched metadata abbreviated
    static QStringList describeJvmArguments(const QStringList& args);

   private:
    static QString cacheDirectory();

    void fetchApiMetadata();
    void fetchLatestVersion();
    void onLatestVersion(const QByteArray& data);
    void useCachedJar();
    void finish();

    AuthSessionPtr m_session;
    QByteArray m_apiMetadata;
    QString m_jarPath;

    NetJob::Ptr m_metadataJob;
    NetJob::Ptr m_versionJob;
    NetJob::Ptr m_downloadJob;
};
