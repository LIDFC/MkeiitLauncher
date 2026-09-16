// SPDX-License-Identifier: GPL-3.0-only
#include "OurServerConfig.h"

#include <QStringList>

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

// stored as one string separated by ';', mod keys never contain it
QSet<QString> enabledOptionalMods()
{
    const auto value = APPLICATION->settings()->get("OurServerOptionalMods").toString();
    const auto keys = value.split(';', Qt::SkipEmptyParts);
    return QSet<QString>(keys.begin(), keys.end());
}

void setOptionalModEnabled(const QString& key, bool enabled)
{
    auto keys = enabledOptionalMods();
    if (enabled) {
        keys.insert(key);
    } else {
        keys.remove(key);
    }
    QStringList sorted(keys.begin(), keys.end());
    sorted.sort();
    APPLICATION->settings()->set("OurServerOptionalMods", sorted.join(';'));
}

}  // namespace OurServer
