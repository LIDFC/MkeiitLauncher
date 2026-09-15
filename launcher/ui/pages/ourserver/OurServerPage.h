// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>
#include <optional>

#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackManifest.h"
#include "ourserver/ServerPackPlan.h"
#include "ourserver/ServerPackTask.h"

class MinecraftInstance;
class ProgressWidget;
class QGroupBox;
class QLabel;
class QPushButton;
class QTreeWidget;

/**
 * The "Our Server" category of the main window: server information, server pack state, the mod list of the pack and the
 * "Play on server" button. Installing and launching go through the regular instance, downloader and launch paths.
 */
class OurServerPage : public QWidget {
    Q_OBJECT
   public:
    explicit OurServerPage(QWidget* parent = nullptr);
    ~OurServerPage() override = default;

    //! called whenever the category is shown, refreshes the state of the server pack
    void opened();

   protected:
    void changeEvent(QEvent* event) override;

   private:
    enum class AfterCheck { Nothing, Install, Play };

    void retranslate();
    void updateView();

    void checkServerPack(AfterCheck afterCheck = AfterCheck::Nothing);
    void installServerPack(bool launchAfterwards);
    void startTask(ServerPackTask* task);
    void onTaskFinished();

    MinecraftInstance* ensureInstance(const ServerPack::Manifest& manifest);
    void launchGame(MinecraftInstance* instance, const ServerPack::Manifest& manifest);

    QLabel* m_titleLabel = nullptr;

    QGroupBox* m_serverGroup = nullptr;
    QLabel* m_addressCaption = nullptr;
    QLabel* m_addressValue = nullptr;
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
};
