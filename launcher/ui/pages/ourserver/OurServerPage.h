// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QTimer>
#include <QWidget>
#include <optional>

#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackManifest.h"
#include "ourserver/ServerPackPlan.h"
#include "ourserver/ServerPackTask.h"
#include "ourserver/ServerStatus.h"
#include "ourserver/ServerStatusTask.h"

class MinecraftInstance;
class ProgressWidget;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;

/**
 * The "Our Server" category of the main window: server information and status, the players online, the server pack
 * state, the mod list of the pack and the "Play on server" button. Installing and launching go through the regular
 * instance, downloader and launch paths.
 */
class OurServerPage : public QWidget {
    Q_OBJECT
   public:
    explicit OurServerPage(QWidget* parent = nullptr);
    ~OurServerPage() override = default;

    //! called whenever the category is shown, refreshes the server status and the state of the server pack
    void opened();

   protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    enum class AfterCheck { Nothing, Install, Play };

    struct StatusTarget {
        QString host;
        quint16 port = 0;
        quint16 queryPort = 0;

        QString key() const { return QString("%1:%2/%3").arg(host).arg(port).arg(queryPort); }
    };

    void retranslate();
    void updateView();
    void updateStatusView();

    void checkServerPack(AfterCheck afterCheck = AfterCheck::Nothing);
    void installServerPack(bool launchAfterwards);
    void startTask(ServerPackTask* task);
    void onTaskFinished();

    std::optional<StatusTarget> statusTarget() const;
    //! onlyIfTargetChanged: only check again when the address or the query port changed since the last check
    void refreshServerStatus(bool onlyIfTargetChanged = false);
    void onStatusFinished();

    MinecraftInstance* ensureInstance(const ServerPack::Manifest& manifest);
    void launchGame(MinecraftInstance* instance, const ServerPack::Manifest& manifest);

    QLabel* m_titleLabel = nullptr;

    QGroupBox* m_serverGroup = nullptr;
    QLabel* m_addressCaption = nullptr;
    QLabel* m_addressValue = nullptr;
    QLabel* m_statusCaption = nullptr;
    QLabel* m_statusValue = nullptr;
    QLabel* m_playersCaption = nullptr;
    QLabel* m_playersValue = nullptr;
    QLabel* m_pingCaption = nullptr;
    QLabel* m_pingValue = nullptr;
    QLabel* m_checkedCaption = nullptr;
    QLabel* m_checkedValue = nullptr;
    QLabel* m_minecraftCaption = nullptr;
    QLabel* m_minecraftValue = nullptr;
    QLabel* m_loaderCaption = nullptr;
    QLabel* m_loaderValue = nullptr;

    QGroupBox* m_packGroup = nullptr;
    QLabel* m_packStatusLabel = nullptr;
    QLabel* m_packDetailsLabel = nullptr;
    QPushButton* m_actionButton = nullptr;
    ProgressWidget* m_progress = nullptr;
    QLabel* m_progressDetailsLabel = nullptr;

    QPushButton* m_playButton = nullptr;
    QPushButton* m_refreshButton = nullptr;

    QGroupBox* m_playersGroup = nullptr;
    QListWidget* m_playerList = nullptr;

    QGroupBox* m_modsGroup = nullptr;
    QTreeWidget* m_modList = nullptr;

    ServerPackTask::Ptr m_task;
    std::optional<ServerPack::Manifest> m_manifest;
    std::optional<ServerPack::Plan> m_plan;
    ServerPack::Lock m_lock;
    QString m_error;
    AfterCheck m_afterCheck = AfterCheck::Nothing;
    bool m_launchAfterApply = false;
    bool m_actionIsRetry = false;

    ServerStatusTask::Ptr m_statusTask;
    QString m_statusKey;  // target of the shown status, empty if the server address is not configured
    std::optional<ServerStatus::Result> m_status;
    QTimer m_statusTimer;
};
