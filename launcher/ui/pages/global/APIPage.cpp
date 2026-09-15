// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (c) 2022 Lenny McLennington <lenny@sneed.church>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "APIPage.h"
#include "ui_APIPage.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabBar>
#include <QValidator>
#include <QVariant>

#include "Application.h"
#include "BuildConfig.h"
#include "net/PasteUpload.h"
#include "ourserver/ServerPackManifest.h"
#include "settings/SettingsObject.h"
#include "tools/BaseProfiler.h"

APIPage::APIPage(QWidget* parent) : QWidget(parent), ui(new Ui::APIPage)
{
    // This is here so you can reorder the entries in the combobox without messing stuff up
    auto comboBoxEntries = { static_cast<int>(PasteUpload::Type::Mclogs), static_cast<int>(PasteUpload::Type::NullPointer),
                             static_cast<int>(PasteUpload::Type::PasteGG), static_cast<int>(PasteUpload::Type::Hastebin) };

    static const QRegularExpression s_validUrlRegExp("https?://.+");

    ui->setupUi(this);

    for (auto pasteType : comboBoxEntries) {
        ui->pasteTypeComboBox->addItem(PasteUpload::Type(pasteType).toString(), pasteType);
    }

    void (QComboBox::*currentIndexChangedSignal)(int)(&QComboBox::currentIndexChanged);
    connect(ui->pasteTypeComboBox, currentIndexChangedSignal, this, &APIPage::updateBaseURLPlaceholder);
    // This function needs to be called even when the ComboBox's index is still in its default state.
    updateBaseURLPlaceholder(ui->pasteTypeComboBox->currentIndex());
    // NOTE: this allows http://, but we replace that with https later anyway
    ui->metaURL->setValidator(new QRegularExpressionValidator(s_validUrlRegExp, ui->metaURL));
    ui->resourceURL->setValidator(new QRegularExpressionValidator(s_validUrlRegExp, ui->resourceURL));
    ui->baseURLEntry->setValidator(new QRegularExpressionValidator(s_validUrlRegExp, ui->baseURLEntry));
    ui->legacyFMLLibsURL->setValidator(new QRegularExpressionValidator(s_validUrlRegExp, ui->legacyFMLLibsURL));
    static const QRegularExpression s_validHttpsUrlRegExp("https://.+");
    ui->ourServerManifestURL->setValidator(new QRegularExpressionValidator(s_validHttpsUrlRegExp, ui->ourServerManifestURL));
    ui->ourServerManifestURL->setPlaceholderText(BuildConfig.OUR_SERVER_MANIFEST_URL);

    ui->metaURL->setPlaceholderText(BuildConfig.META_URL);
    ui->resourceURL->setPlaceholderText(BuildConfig.DEFAULT_RESOURCE_BASE);
    ui->legacyFMLLibsURL->setPlaceholderText(BuildConfig.LEGACY_FMLLIBS_BASE_URL);
    ui->userAgentLineEdit->setPlaceholderText(BuildConfig.USER_AGENT);

    loadSettings();

    resetBaseURLNote();
    connect(ui->pasteTypeComboBox, currentIndexChangedSignal, this, &APIPage::updateBaseURLNote);
    connect(ui->baseURLEntry, &QLineEdit::textEdited, this, &APIPage::resetBaseURLNote);
}

APIPage::~APIPage()
{
    delete ui;
}

void APIPage::resetBaseURLNote()
{
    ui->baseURLNote->hide();
    baseURLPasteType = ui->pasteTypeComboBox->currentIndex();
}

void APIPage::updateBaseURLNote(int index)
{
    if (baseURLPasteType == index) {
        ui->baseURLNote->hide();
    } else if (!ui->baseURLEntry->text().isEmpty()) {
        ui->baseURLNote->show();
    }
}

void APIPage::updateBaseURLPlaceholder(int index)
{
    int pasteType = ui->pasteTypeComboBox->itemData(index).toInt();
    QString pasteDefaultURL = PasteUpload::Type(pasteType).defaultBase();
    ui->baseURLEntry->setPlaceholderText(pasteDefaultURL);
}

void APIPage::loadSettings()
{
    auto* s = APPLICATION->settings();

    int pasteType = s->get("PastebinType").toInt();
    QString pastebinURL = s->get("PastebinCustomAPIBase").toString();

    ui->baseURLEntry->setText(pastebinURL);
    int pasteTypeIndex = ui->pasteTypeComboBox->findData(pasteType);
    if (pasteTypeIndex == -1) {
        pasteTypeIndex = ui->pasteTypeComboBox->findData(static_cast<int>(PasteUpload::Type::Mclogs));
        ui->baseURLEntry->clear();
    }

    ui->pasteTypeComboBox->setCurrentIndex(pasteTypeIndex);

    if (bool fallbackMRBlockedMods = s->get("FallbackMRBlockedMods").toBool()) {
        ui->FallbackMRBlockedMods->setChecked(fallbackMRBlockedMods);
    }

    QString elyByClientID = s->get("ElyByClientIDOverride").toString();
    ui->elyByClientID->setText(elyByClientID);
    QString metaURL = s->get("MetaURLOverride").toString();
    ui->metaURL->setText(metaURL);
    ui->metaRefreshOnLaunchCB->setCheckState(s->get("MetaRefreshOnLaunch").toBool() ? Qt::Checked : Qt::Unchecked);
    QString resourceURL = s->get("ResourceURLOverride").toString();
    ui->resourceURL->setText(resourceURL);
    QString fmlLibsURL = s->get("LegacyFMLLibsURLOverride").toString();
    ui->legacyFMLLibsURL->setText(fmlLibsURL);
    QString flameKey = s->get("FlameKeyOverride").toString();
    ui->flameKey->setText(flameKey);
    QString modrinthToken = s->get("ModrinthToken").toString();
    ui->modrinthToken->setText(modrinthToken);
    QString customUserAgent = s->get("UserAgentOverride").toString();
    ui->userAgentLineEdit->setText(customUserAgent);
    ui->technicClientID->setText(s->get("TechnicClientID").toString());
    ui->ourServerManifestURL->setText(s->get("OurServerManifestURLOverride").toString());
    ui->ourServerAddress->setText(s->get("OurServerAddressOverride").toString());
}

void APIPage::applySettings()
{
    auto* s = APPLICATION->settings();

    s->set("PastebinType", ui->pasteTypeComboBox->currentData().toInt());
    s->set("PastebinCustomAPIBase", ui->baseURLEntry->text());

    QString elyByClientID = ui->elyByClientID->text().trimmed();
    s->set("ElyByClientIDOverride", elyByClientID);
    QUrl metaURL(ui->metaURL->text());
    QUrl resourceURL(ui->resourceURL->text());
    QUrl fmlLibsURL(ui->legacyFMLLibsURL->text());

    auto addRequiredTrailingSlash = [](QUrl& url) {
        if (!url.isEmpty() && !url.path().endsWith('/')) {
            QString path = url.path();
            path.append('/');
            url.setPath(path);
        }
    };
    addRequiredTrailingSlash(metaURL);
    addRequiredTrailingSlash(resourceURL);
    addRequiredTrailingSlash(fmlLibsURL);

    auto isLocalhost = [](const QUrl& url) { return url.host() == "localhost" || url.host() == "127.0.0.1" || url.host() == "::1"; };
    auto isUnsafe = [isLocalhost](const QUrl& url) { return !url.isEmpty() && url.scheme() == "http" && !isLocalhost(url); };
    auto upgradeToHTTPS = [isUnsafe](QUrl& url) {
        if (isUnsafe(url)) {
            url.setScheme("https");
        }
    };

    upgradeToHTTPS(metaURL);
    upgradeToHTTPS(resourceURL);
    upgradeToHTTPS(fmlLibsURL);

    s->set("FallbackMRBlockedMods", ui->FallbackMRBlockedMods->checkState());
    s->set("MetaURLOverride", metaURL.toString());
    s->set("MetaRefreshOnLaunch", ui->metaRefreshOnLaunchCB->checkState() == Qt::Checked);
    s->set("ResourceURLOverride", resourceURL.toString());
    s->set("LegacyFMLLibsURLOverride", fmlLibsURL.toString());
    QString flameKey = ui->flameKey->text();
    s->set("FlameKeyOverride", flameKey);
    QString modrinthToken = ui->modrinthToken->text();
    s->set("ModrinthToken", modrinthToken);
    s->set("UserAgentOverride", ui->userAgentLineEdit->text());
    s->set("TechnicClientID", ui->technicClientID->text());

    // the server pack manifest is only ever fetched over HTTPS
    const QString ourServerManifestURL = ui->ourServerManifestURL->text().trimmed();
    s->set("OurServerManifestURLOverride", ServerPack::isSecureUrl(QUrl(ourServerManifestURL)) ? ourServerManifestURL : QString());
    s->set("OurServerAddressOverride", ui->ourServerAddress->text().trimmed());
}

bool APIPage::apply()
{
    applySettings();
    return true;
}

void APIPage::retranslate()
{
    ui->retranslateUi(this);
}
