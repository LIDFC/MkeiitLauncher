// SPDX-License-Identifier: GPL-3.0-only
#include "OurServerPage.h"

#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

#include "Application.h"
#include "InstanceList.h"
#include "StringUtils.h"
#include "minecraft/MinecraftInstance.h"
#include "ourserver/OurServerConfig.h"
#include "ourserver/OurServerInstance.h"
#include "ourserver/ServerPackLogging.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/widgets/ProgressWidget.h"

namespace {
constexpr int STATUS_REFRESH_INTERVAL_MS = 30000;

QString loaderDisplayName(const QString& type)
{
    if (type == "fabric") {
        return "Fabric";
    }
    if (type == "quilt") {
        return "Quilt";
    }
    if (type == "forge") {
        return "Forge";
    }
    if (type == "neoforge") {
        return "NeoForge";
    }
    return type;
}

QLabel* createValueLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QListWidgetItem* addPlaceholderItem(QListWidget* list, const QString& text)
{
    auto* item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
    QFont font = item->font();
    font.setItalic(true);
    item->setFont(font);
    return item;
}
}  // namespace

OurServerPage::OurServerPage(QWidget* parent) : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 12, 16, 12);
    rootLayout->setSpacing(10);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    rootLayout->addWidget(m_titleLabel);

    auto* columns = new QHBoxLayout();
    columns->setSpacing(12);
    rootLayout->addLayout(columns, 1);

    auto* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(10);
    columns->addLayout(leftColumn, 2);

    // server information and status
    m_serverGroup = new QGroupBox(this);
    auto* serverLayout = new QFormLayout(m_serverGroup);
    m_addressCaption = new QLabel(m_serverGroup);
    m_addressValue = createValueLabel(m_serverGroup);
    m_statusCaption = new QLabel(m_serverGroup);
    m_statusValue = createValueLabel(m_serverGroup);
    QFont serverStatusFont = m_statusValue->font();
    serverStatusFont.setBold(true);
    m_statusValue->setFont(serverStatusFont);
    m_playersCaption = new QLabel(m_serverGroup);
    m_playersValue = createValueLabel(m_serverGroup);
    m_pingCaption = new QLabel(m_serverGroup);
    m_pingValue = createValueLabel(m_serverGroup);
    m_checkedCaption = new QLabel(m_serverGroup);
    m_checkedValue = createValueLabel(m_serverGroup);
    m_minecraftCaption = new QLabel(m_serverGroup);
    m_minecraftValue = createValueLabel(m_serverGroup);
    m_loaderCaption = new QLabel(m_serverGroup);
    m_loaderValue = createValueLabel(m_serverGroup);
    serverLayout->addRow(m_addressCaption, m_addressValue);
    serverLayout->addRow(m_statusCaption, m_statusValue);
    serverLayout->addRow(m_playersCaption, m_playersValue);
    serverLayout->addRow(m_pingCaption, m_pingValue);
    serverLayout->addRow(m_checkedCaption, m_checkedValue);
    serverLayout->addRow(m_minecraftCaption, m_minecraftValue);
    serverLayout->addRow(m_loaderCaption, m_loaderValue);
    leftColumn->addWidget(m_serverGroup);

    // state of the server pack
    m_packGroup = new QGroupBox(this);
    auto* packLayout = new QVBoxLayout(m_packGroup);
    m_packStatusLabel = new QLabel(m_packGroup);
    QFont statusFont = m_packStatusLabel->font();
    statusFont.setBold(true);
    m_packStatusLabel->setFont(statusFont);
    m_packStatusLabel->setWordWrap(true);
    m_packDetailsLabel = new QLabel(m_packGroup);
    m_packDetailsLabel->setWordWrap(true);
    m_packDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_actionButton = new QPushButton(m_packGroup);
    m_progress = new ProgressWidget(m_packGroup);
    m_progress->hideIfInactive(true);
    m_progress->hide();
    m_progressDetailsLabel = new QLabel(m_packGroup);
    packLayout->addWidget(m_packStatusLabel);
    packLayout->addWidget(m_packDetailsLabel);
    packLayout->addWidget(m_actionButton);
    packLayout->addWidget(m_progress);
    packLayout->addWidget(m_progressDetailsLabel);
    leftColumn->addWidget(m_packGroup);
    leftColumn->addStretch(1);

    m_playButton = new QPushButton(this);
    m_playButton->setIcon(QIcon::fromTheme("launch"));
    m_playButton->setMinimumHeight(48);
    QFont playFont = m_playButton->font();
    playFont.setPointSizeF(playFont.pointSizeF() * 1.3);
    playFont.setBold(true);
    m_playButton->setFont(playFont);
    leftColumn->addWidget(m_playButton);

    m_refreshButton = new QPushButton(this);
    m_refreshButton->setIcon(QIcon::fromTheme("refresh"));
    leftColumn->addWidget(m_refreshButton, 0, Qt::AlignLeft);

    auto* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(10);
    columns->addLayout(rightColumn, 3);

    // players online
    m_playersGroup = new QGroupBox(this);
    auto* playersLayout = new QVBoxLayout(m_playersGroup);
    m_playerList = new QListWidget(m_playersGroup);
    m_playerList->setSelectionMode(QAbstractItemView::NoSelection);
    playersLayout->addWidget(m_playerList);
    rightColumn->addWidget(m_playersGroup, 1);

    // mods of the server pack
    m_modsGroup = new QGroupBox(this);
    auto* modsLayout = new QVBoxLayout(m_modsGroup);
    m_modList = new QTreeWidget(m_modsGroup);
    m_modList->setColumnCount(4);
    m_modList->setRootIsDecorated(false);
    m_modList->setSelectionMode(QAbstractItemView::NoSelection);
    m_modList->setAlternatingRowColors(true);
    m_modList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; column++) {
        m_modList->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    modsLayout->addWidget(m_modList);
    rightColumn->addWidget(m_modsGroup, 2);

    connect(m_actionButton, &QPushButton::clicked, this, [this] {
        if (m_actionIsRetry) {
            checkServerPack();
        } else {
            installServerPack(false);
        }
    });
    connect(m_playButton, &QPushButton::clicked, this, [this] { checkServerPack(AfterCheck::Play); });
    connect(m_refreshButton, &QPushButton::clicked, this, [this] {
        checkServerPack();
        refreshServerStatus();
    });

    // the status is only refreshed while the category is visible
    m_statusTimer.setInterval(STATUS_REFRESH_INTERVAL_MS);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] { refreshServerStatus(); });

    retranslate();
}

void OurServerPage::opened()
{
    checkServerPack();
    refreshServerStatus();
}

void OurServerPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void OurServerPage::showEvent(QShowEvent* event)
{
    m_statusTimer.start();
    QWidget::showEvent(event);
}

void OurServerPage::hideEvent(QHideEvent* event)
{
    m_statusTimer.stop();
    QWidget::hideEvent(event);
}

void OurServerPage::retranslate()
{
    m_serverGroup->setTitle(tr("Server"));
    m_addressCaption->setText(tr("Address"));
    m_statusCaption->setText(tr("Status"));
    m_playersCaption->setText(tr("Players"));
    m_pingCaption->setText(tr("Ping"));
    m_checkedCaption->setText(tr("Last checked"));
    m_minecraftCaption->setText(tr("Minecraft"));
    m_loaderCaption->setText(tr("Loader"));
    m_playersGroup->setTitle(tr("Players online"));
    m_packGroup->setTitle(tr("Server pack"));
    m_modList->setHeaderLabels({ tr("Mod"), tr("Status"), tr("Installed"), tr("Required") });
    m_playButton->setText(tr("▶ Play on server"));
    m_refreshButton->setText(tr("Check again"));
    updateView();
}

void OurServerPage::checkServerPack(AfterCheck afterCheck)
{
    if (m_task && m_task->isRunning()) {
        return;
    }
    m_afterCheck = afterCheck;
    const auto* instance = OurServer::findInstance();
    startTask(new ServerPackTask(OurServer::manifestUrl(), instance ? instance->modsRoot() : QString(),
                                 instance ? OurServer::lockPath(instance) : QString(), ServerPackTask::Mode::Check));
}

void OurServerPage::installServerPack(bool launchAfterwards)
{
    if (m_task && m_task->isRunning()) {
        return;
    }
    if (!m_manifest) {
        checkServerPack(launchAfterwards ? AfterCheck::Play : AfterCheck::Install);
        return;
    }

    auto* instance = ensureInstance(*m_manifest);
    if (!instance) {
        updateView();
        return;
    }
    if (instance->isRunning()) {
        if (launchAfterwards) {
            launchGame(instance, *m_manifest);
        } else {
            CustomMessageBox::selectable(this, tr("Server pack"), tr("Close the game before updating the server pack."),
                                         QMessageBox::Information)
                ->show();
        }
        return;
    }

    OurServer::applyComponents(instance, *m_manifest);
    m_launchAfterApply = launchAfterwards;
    startTask(
        new ServerPackTask(OurServer::manifestUrl(), instance->modsRoot(), OurServer::lockPath(instance), ServerPackTask::Mode::Apply));
}

void OurServerPage::startTask(ServerPackTask* task)
{
    m_task.reset(task);
    m_error.clear();
    m_progressDetailsLabel->clear();
    connect(task, &Task::finished, this, &OurServerPage::onTaskFinished);
    connect(task, &Task::details, m_progressDetailsLabel, &QLabel::setText);
    m_progress->watch(task);
    task->start();
    updateView();
}

void OurServerPage::onTaskFinished()
{
    const auto task = m_task;
    if (!task) {
        return;
    }

    const bool succeeded = task->wasSuccessful();
    const bool aborted = task->getState() == Task::State::AbortedByUser;
    if (task->hasManifest()) {
        m_manifest = task->manifest();
        // the manifest can change the server address or the query port
        refreshServerStatus(true);
    }
    if (task->hasPlan()) {
        m_plan = task->plan();
        m_lock = task->installedLock();
    } else if (!succeeded) {
        m_plan.reset();
    }
    if (!succeeded && !aborted) {
        m_error = task->failReason();
    }
    m_progressDetailsLabel->clear();

    if (task->mode() == ServerPackTask::Mode::Check) {
        const auto afterCheck = std::exchange(m_afterCheck, AfterCheck::Nothing);
        if (!succeeded) {
            updateView();
            auto* instance = OurServer::findInstance();
            if (afterCheck == AfterCheck::Play && !aborted && instance) {
                const auto answer =
                    CustomMessageBox::selectable(
                        this, tr("Server pack"),
                        tr("The server pack could not be checked:\n%1\n\nPlay with the mods that are already installed?").arg(m_error),
                        QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();
                if (answer == QMessageBox::Yes) {
                    launchGame(instance, m_manifest ? *m_manifest : ServerPack::Manifest{});
                }
            }
            return;
        }

        if (afterCheck == AfterCheck::Play) {
            auto* instance = OurServer::findInstance();
            if (instance && m_plan && m_plan->isUpToDate()) {
                OurServer::applyComponents(instance, *m_manifest);
                updateView();
                launchGame(instance, *m_manifest);
                return;
            }
            installServerPack(true);
            return;
        }
        if (afterCheck == AfterCheck::Install) {
            installServerPack(false);
            return;
        }
        updateView();
        return;
    }

    // Apply
    const bool launchAfterwards = std::exchange(m_launchAfterApply, false);
    auto* instance = OurServer::findInstance();
    if (succeeded && instance) {
        OurServer::applyComponents(instance, task->manifest());
        OurServer::markInstalled(instance, task->manifest());
        updateView();
        if (launchAfterwards) {
            launchGame(instance, task->manifest());
        }
        return;
    }
    updateView();
    if (!succeeded && !aborted) {
        CustomMessageBox::selectable(this, tr("Server pack"), m_error, QMessageBox::Warning)->show();
    }
}

std::optional<OurServerPage::StatusTarget> OurServerPage::statusTarget() const
{
    // the same address the game joins
    const auto join = OurServer::joinTarget(m_manifest ? *m_manifest : ServerPack::Manifest{});
    if (!join || join->address.isEmpty()) {
        return std::nullopt;
    }
    return StatusTarget{ join->address, join->port, m_manifest ? static_cast<quint16>(m_manifest->queryPort) : quint16(0) };
}

void OurServerPage::refreshServerStatus(bool onlyIfTargetChanged)
{
    if (m_statusTask && m_statusTask->isRunning()) {
        // a finished check calls this again, so a changed target is not missed
        return;
    }
    const auto target = statusTarget();
    const QString key = target ? target->key() : QString();
    if (onlyIfTargetChanged && key == m_statusKey && (m_status || key.isEmpty())) {
        return;
    }
    if (key != m_statusKey) {
        // never show the status of a different server
        m_status.reset();
        m_statusKey = key;
    }
    if (!target) {
        updateStatusView();
        return;
    }

    m_statusTask.reset(new ServerStatusTask(target->host, target->port, target->queryPort));
    connect(m_statusTask.get(), &Task::finished, this, &OurServerPage::onStatusFinished);
    m_statusTask->start();
    updateStatusView();
}

void OurServerPage::onStatusFinished()
{
    const auto task = m_statusTask;
    if (!task || !task->wasSuccessful()) {
        updateStatusView();
        return;
    }

    const auto& result = task->result();
    if (!m_status || m_status->online != result.online) {
        if (result.online) {
            qCInfo(serverPackLogC).noquote() << "[ServerStatus]" << result.target << "is online";
        } else {
            qCInfo(serverPackLogC).noquote() << "[ServerStatus]" << result.target << "is unavailable:" << result.error;
        }
    }
    if (!result.queryError.isEmpty() && (!m_status || m_status->queryError != result.queryError)) {
        qCWarning(serverPackLogC).noquote() << "[ServerStatus] Query failed, only a part of the player list is shown:" << result.queryError;
    }
    m_status = result;
    updateStatusView();

    // the address may have changed while the server was checked
    refreshServerStatus(true);
}

MinecraftInstance* OurServerPage::ensureInstance(const ServerPack::Manifest& manifest)
{
    if (auto* instance = OurServer::findInstance()) {
        return instance;
    }

    const unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(new OurServer::CreateInstanceTask(manifest)));
    ProgressDialog dialog(this);
    dialog.setSkipButton(true, tr("Abort"));
    dialog.execWithTask(task.get());

    if (!task->wasSuccessful()) {
        if (task->getState() != Task::State::AbortedByUser) {
            CustomMessageBox::selectable(this, tr("Server pack"), tr("Could not create the server instance: %1").arg(task->failReason()),
                                         QMessageBox::Critical)
                ->show();
        }
        return nullptr;
    }

    auto* instance = OurServer::findInstance();
    if (!instance) {
        APPLICATION->instances()->loadList();
        instance = OurServer::findInstance();
    }
    return instance;
}

void OurServerPage::launchGame(MinecraftInstance* instance, const ServerPack::Manifest& manifest)
{
    auto target = OurServer::joinTarget(manifest);
    qCInfo(serverPackLogC) << "[ServerPack] Launching" << instance->id()
                           << (target ? "and joining the server" : "without joining a server");
    // the account is chosen by the regular launch flow (Guest / Offline or Ely.by)
    APPLICATION->launch(instance, LaunchMode::Normal, target);
}

void OurServerPage::updateView()
{
    const bool busy = m_task && m_task->isRunning();
    const ServerPack::Manifest* manifest = m_manifest ? &*m_manifest : nullptr;

    m_titleLabel->setText(manifest && !manifest->name.isEmpty() ? manifest->name : tr("Our Server"));

    // server information
    QString address = OurServer::serverAddressOverride();
    if (address.isEmpty() && manifest && !manifest->serverAddress.isEmpty()) {
        address = manifest->serverPort == ServerPack::DEFAULT_SERVER_PORT
                      ? manifest->serverAddress
                      : QString("%1:%2").arg(manifest->serverAddress).arg(manifest->serverPort);
    }
    m_addressValue->setText(address.isEmpty() ? tr("Not configured yet") : address);
    m_minecraftValue->setText(manifest ? manifest->minecraft : QString("—"));
    m_loaderValue->setText(manifest ? QString("%1 %2").arg(loaderDisplayName(manifest->loaderType), manifest->loaderVersion)
                                    : QString("—"));

    // state of the server pack
    QString status;
    QString details;
    QString action;
    bool actionEnabled = false;
    m_actionIsRetry = false;

    if (!m_error.isEmpty()) {
        status = tr("⚠ Server pack problem");
        details = m_error;
        action = tr("Try again");
        actionEnabled = true;
        m_actionIsRetry = true;
    } else if (manifest && m_plan) {
        const auto& plan = *m_plan;
        if (!plan.conflicts.isEmpty()) {
            status = tr("✗ Files in the mods folder block the server pack");
            details = tr("Remove or rename these files: %1").arg(plan.conflicts.join(", "));
        } else if (plan.isUpToDate()) {
            status = tr("✓ Server pack %1").arg(manifest->packVersion);
            details = tr("All required mods are installed.");
            action = tr("✓ Server pack up to date");
        } else if (plan.needsUpdate()) {
            status = tr("🔄 Server pack update available");
            QStringList parts;
            if (ServerPack::isNewerPackVersion(m_lock.packVersion, manifest->packVersion) && !m_lock.packVersion.isEmpty()) {
                parts << QString("%1 → %2").arg(m_lock.packVersion, manifest->packVersion);
            }
            if (!plan.downloads.isEmpty()) {
                parts << tr("%n file(s)", "", plan.downloads.size()) << StringUtils::humanReadableFileSize(plan.downloadSize);
            }
            details = parts.join(" · ");
            action = tr("🔄 Update server pack");
            actionEnabled = true;
        } else {
            status = tr("⬇ Mods of the server pack are not installed");
            details = tr("%n file(s)", "", plan.downloads.size()) + " · " + StringUtils::humanReadableFileSize(plan.downloadSize);
            action = tr("⬇ Install %n mod(s)", "", plan.downloads.size());
            actionEnabled = true;
        }
    } else if (busy) {
        status = tr("Checking the server pack...");
    }

    m_packStatusLabel->setText(status);
    m_packStatusLabel->setVisible(!status.isEmpty());
    m_packDetailsLabel->setText(details);
    m_packDetailsLabel->setVisible(!details.isEmpty());
    m_actionButton->setText(action);
    m_actionButton->setVisible(!action.isEmpty());
    m_actionButton->setEnabled(actionEnabled && !busy);

    // mods of the server pack
    m_modsGroup->setTitle(manifest ? tr("Server pack %1").arg(manifest->packVersion) : tr("Mods of the server pack"));
    m_modList->clear();
    if (m_plan) {
        for (const auto& mod : m_plan->mods) {
            auto* item = new QTreeWidgetItem(m_modList);
            item->setText(0, mod.target.mod.name);
            item->setToolTip(0, mod.target.fileName);
            switch (mod.state) {
                case ServerPack::ModState::UpToDate:
                    item->setText(1, tr("✓ Installed"));
                    break;
                case ServerPack::ModState::Missing:
                    item->setText(1, tr("✗ Not installed"));
                    break;
                case ServerPack::ModState::Outdated:
                    item->setText(1, tr("🔄 Update required"));
                    break;
                case ServerPack::ModState::Corrupted:
                    item->setText(1, tr("⚠ Damaged, will be downloaded again"));
                    break;
                case ServerPack::ModState::Conflict:
                    item->setText(1, tr("✗ Blocked by another file"));
                    break;
            }
            item->setText(2, mod.installedVersion.isEmpty() ? QString("—") : mod.installedVersion);
            item->setText(3, mod.target.versionNumber);
        }
    }

    const bool hasConflicts = m_plan && !m_plan->conflicts.isEmpty();
    m_playButton->setEnabled(!busy && !hasConflicts && (manifest != nullptr || OurServer::findInstance() != nullptr));
    m_playButton->setToolTip(address.isEmpty() ? tr("The server address is not configured yet, the game starts without joining a server.")
                                               : QString());
    m_refreshButton->setEnabled(!busy);

    updateStatusView();
}

void OurServerPage::updateStatusView()
{
    const QString unknown = QStringLiteral("—");
    const bool checking = m_statusTask && m_statusTask->isRunning();

    m_playerList->clear();
    m_statusValue->setToolTip(QString());

    if (!m_status) {
        // nothing is known about this server (yet)
        m_statusValue->setText(checking ? tr("Checking...") : unknown);
        m_playersValue->setText(unknown);
        m_pingValue->setText(unknown);
        m_checkedValue->setText(unknown);
        return;
    }

    const auto& status = *m_status;
    m_checkedValue->setText(status.checkedAt.time().toString("HH:mm:ss"));
    if (!status.online) {
        m_statusValue->setText(tr("🔴 Unavailable"));
        m_statusValue->setToolTip(status.error);
        m_playersValue->setText(unknown);
        m_pingValue->setText(unknown);
        addPlaceholderItem(m_playerList, tr("No data"));
        return;
    }

    const auto& players = status.players;
    m_statusValue->setText(tr("🟢 Online"));
    if (players.online < 0) {
        m_playersValue->setText(unknown);
    } else if (players.max < 0) {
        m_playersValue->setText(QString::number(players.online));
    } else {
        m_playersValue->setText(QString("%1 / %2").arg(players.online).arg(players.max));
    }
    m_pingValue->setText(status.latencyMs < 0 ? unknown : tr("%1 ms").arg(status.latencyMs));

    if (players.online < 0) {
        addPlaceholderItem(m_playerList, tr("The server does not report its players."));
        return;
    }
    if (players.online == 0 && players.names.isEmpty()) {
        addPlaceholderItem(m_playerList, tr("Nobody is playing right now"));
        return;
    }
    for (const auto& name : players.names) {
        new QListWidgetItem(name, m_playerList);
    }
    if (const int unlisted = players.unlistedCount(); unlisted > 0) {
        auto* item = addPlaceholderItem(m_playerList, tr("…and %n more", "", unlisted));
        if (!status.queryError.isEmpty()) {
            item->setToolTip(tr("The full player list could not be requested: %1").arg(status.queryError));
        } else if (!status.fullPlayerList) {
            item->setToolTip(
                tr("The server shows only a part of the player list. The full list needs enable-query=true on the server and "
                   "\"queryPort\" in the manifest."));
        }
    }
}
