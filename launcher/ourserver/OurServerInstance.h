// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <memory>

#include "InstanceTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "ourserver/ServerPackManifest.h"

/**
 * The regular Prism instance used to play on "Our Server". It is recognized by its managed pack type, so the player can
 * rename or move it without breaking the category.
 */
namespace OurServer {

MinecraftInstance* findInstance();

QString lockPath(const MinecraftInstance* instance);

//! Sets the Minecraft and loader versions of the manifest, returns true if the instance changed
bool applyComponents(MinecraftInstance* instance, const ServerPack::Manifest& manifest);

//! Records the installed pack version in the instance settings
void markInstalled(MinecraftInstance* instance, const ServerPack::Manifest& manifest);

//! Server to join after launching, nullptr if no address is configured
MinecraftTarget::Ptr joinTarget(const ServerPack::Manifest& manifest);

class CreateInstanceTask final : public InstanceTask {
    Q_OBJECT
   public:
    explicit CreateInstanceTask(ServerPack::Manifest manifest);
    ~CreateInstanceTask() override = default;

    void executeTask() override;

   private:
    ServerPack::Manifest m_manifest;
    std::unique_ptr<MinecraftInstance> m_instance;
};

}  // namespace OurServer
