// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QUrl>
#include <QtWidgets/QDialog>

#include "minecraft/auth/AuthFlow.h"
#include "minecraft/auth/MinecraftAccount.h"

namespace Ui {
class ElyByLoginDialog;
}

/**
 * Adds an Ely.by account. The user logs in on the official Ely.by page in their browser.
 */
class ElyByLoginDialog : public QDialog {
    Q_OBJECT

   public:
    ~ElyByLoginDialog() override;

    static MinecraftAccountPtr newAccount(QWidget* parent);
    int exec() override;

   private:
    explicit ElyByLoginDialog(QWidget* parent = nullptr);

   protected slots:
    void onTaskFailed(QString reason);
    void onTaskStatus(QString status);
    void authorizeWithBrowser(QString url, QString code, int expiresIn);

   private:
    void openBrowser();

    Ui::ElyByLoginDialog* ui;
    MinecraftAccountPtr m_account;
    shared_qobject_ptr<AuthFlow> m_task;

    QUrl m_url;
    bool m_browserOpened = false;
};
