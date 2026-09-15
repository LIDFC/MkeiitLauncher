// SPDX-License-Identifier: GPL-3.0-only
#include "OurServerConfig.h"

#include "Application.h"
#include "BuildConfig.h"
#include "settings/SettingsObject.h"

namespace OurServer {

QUrl manifestUrl()
{
    const auto manifestOverride = APPLICATION->settings()->get("OurServerManifestURLOverride").toString().trimmed();
    return QUrl(manifestOverride.isEmpty() ? BuildConfig.OUR_SERVER_MANIFEST_URL : manifestOverride);
}

QString serverAddressOverride()
{
    return APPLICATION->settings()->get("OurServerAddressOverride").toString().trimmed();
}

}  // namespace OurServer
