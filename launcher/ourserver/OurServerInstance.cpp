// SPDX-License-Identifier: GPL-3.0-only
#include "OurServerInstance.h"

#include <QStringList>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/PackProfile.h"
#include "ourserver/OurServerConfig.h"
#include "ourserver/ServerPackLock.h"
#include "ourserver/ServerPackLogging.h"
#include "settings/INISettingsObject.h"

namespace OurServer {

namespace {
QString instanceName(const ServerPack::Manifest& manifest)
{
    return manifest.name.isEmpty() ? QString(DEFAULT_INSTANCE_NAME) : manifest.name;
}

const QStringList& loaderUids()
{
    static const QStringList s_uids{ "net.fabricmc.fabric-loader", "org.quiltmc.quilt-loader", "net.minecraftforge", "net.neoforged" };
    return s_uids;
}
}  // namespace

MinecraftInstance* findInstance()
{
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); i++) {
        auto* instance = instances->at(i);
        if (instance->isManagedPack() && instance->getManagedPackType() == MANAGED_PACK_TYPE) {
            return instance;
        }
    }
    return nullptr;
}

QString lockPath(const MinecraftInstance* instance)
{
    return FS::PathCombine(instance->instanceRoot(), ServerPack::LOCK_FILE_NAME);
}

bool applyComponents(MinecraftInstance* instance, const ServerPack::Manifest& manifest)
{
    const auto loaderUid = ServerPack::loaderComponentUid(manifest.loaderType);
    if (loaderUid.isEmpty() || manifest.minecraft.isEmpty()) {
        return false;
    }

    auto* profile = instance->getPackProfile();
    // make sure the components on disk are loaded, so saving can never drop any of them
    static_cast<void>(profile->reload(Net::Mode::Offline));

    bool changed = false;
    for (const auto& uid : loaderUids()) {
        if (uid != loaderUid && profile->getComponent(uid)) {
            profile->remove(uid);
            changed = true;
        }
    }
    if (profile->getComponentVersion("net.minecraft") != manifest.minecraft) {
        profile->setComponentVersion("net.minecraft", manifest.minecraft, true);
        changed = true;
    }
    if (profile->getComponentVersion(loaderUid) != manifest.loaderVersion) {
        profile->setComponentVersion(loaderUid, manifest.loaderVersion);
        changed = true;
    }

    if (changed) {
        qCInfo(serverPackLogC).noquote() << "[ServerPack] Instance set to Minecraft" << manifest.minecraft << "with" << manifest.loaderType
                                         << manifest.loaderVersion;
        profile->saveNow();
    }
    return changed;
}

void markInstalled(MinecraftInstance* instance, const ServerPack::Manifest& manifest)
{
    instance->setManagedPack(MANAGED_PACK_TYPE, manifestUrl().toString(), instanceName(manifest), manifest.packVersion,
                             manifest.packVersion);
}

MinecraftTarget::Ptr joinTarget(const ServerPack::Manifest& manifest)
{
    const auto addressOverride = serverAddressOverride();
    if (!addressOverride.isEmpty()) {
        return std::make_shared<MinecraftTarget>(MinecraftTarget::parse(addressOverride, false));
    }
    if (manifest.serverAddress.isEmpty()) {
        return nullptr;
    }
    return std::make_shared<MinecraftTarget>(
        MinecraftTarget{ manifest.serverAddress, static_cast<quint16>(manifest.serverPort), QString() });
}

CreateInstanceTask::CreateInstanceTask(ServerPack::Manifest manifest) : m_manifest(std::move(manifest))
{
    setName(instanceName(m_manifest));
    setGroup(instanceName(m_manifest));
    setIcon("default");
}

void CreateInstanceTask::executeTask()
{
    setStatus(tr("Creating the server instance..."));
    qCInfo(serverPackLogC) << "[ServerPack] Creating the server instance";

    m_instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
    {
        const SettingsObject::Lock lock(m_instance->settings());

        auto* components = m_instance->getPackProfile();
        components->buildingFromScratch();
        components->setComponentVersion("net.minecraft", m_manifest.minecraft, true);
        components->setComponentVersion(ServerPack::loaderComponentUid(m_manifest.loaderType), m_manifest.loaderVersion);

        m_instance->setName(name());
        m_instance->setIconKey(m_instIcon);
        // the pack version is recorded once the server pack is actually installed
        m_instance->setManagedPack(MANAGED_PACK_TYPE, manifestUrl().toString(), name(), QString(), QString());

        components->saveNow();
    }

    downloadFiles(m_instance.get());
}

}  // namespace OurServer
