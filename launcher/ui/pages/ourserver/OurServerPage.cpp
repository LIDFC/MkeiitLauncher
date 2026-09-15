// SPDX-License-Identifier: GPL-3.0-only
#include "OurServerPage.h"

#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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

    // server information
    m_serverGroup = new QGroupBox(this);
    auto* serverLayout = new QFormLayout(m_serverGroup);
    m_addressCaption = new QLabel(m_serverGroup);
    m_addressValue = createValueLabel(m_serverGroup);
    m_minecraftCaption = new QLabel(m_serverGroup);
    m_minecraftValue = createValueLabel(m_serverGroup);
    m_loaderCaption = new QLabel(m_serverGroup);
    m_loaderValue = createValueLabel(m_serverGroup);
    serverLayout->addRow(m_addressCaption, m_addressValue);
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
    columns->addWidget(m_modsGroup, 3);

    connect(m_actionButton, &QPushButton::clicked, this, [this] {
        if (m_actionIsRetry) {
            checkServerPack();
        } else {
            installServerPack(false);
        }
    });
    connect(m_playButton, &QPushButton::clicked, this, [this] { checkServerPack(AfterCheck::Play); });
    connect(m_refreshButton, &QPushButton::clicked, this, [this] { checkServerPack(); });

    retranslate();
}

void OurServerPage::opened()
{
    checkServerPack();
}

void OurServerPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void OurServerPage::retranslate()
{
    m_serverGroup->setTitle(tr("Server"));
    m_addressCaption->setText(tr("Address"));
    m_minecraftCaption->setText(tr("Minecraft"));
    m_loaderCaption->setText(tr("Loader"));
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
}
