// SPDX-License-Identifier: GPL-3.0-only
#include "ElyByLoginDialog.h"
#include "ui_ElyByLoginDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>

#include "Application.h"
#include "DesktopServices.h"
#include "settings/SettingsObject.h"

ElyByLoginDialog::ElyByLoginDialog(QWidget* parent) : QDialog(parent), ui(new Ui::ElyByLoginDialog)
{
    ui->setupUi(this);

    // make font monospace
    QFont font;
    font.setPixelSize(ui->code->fontInfo().pixelSize());
    font.setFamily(APPLICATION->settings()->get("ConsoleFont").toString());
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    ui->code->setFont(font);

    ui->stackedWidget->setCurrentIndex(0);

    connect(ui->copyCode, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(ui->code->text()); });
    connect(ui->loginButton, &QPushButton::clicked, this, &ElyByLoginDialog::openBrowser);

    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
}

ElyByLoginDialog::~ElyByLoginDialog()
{
    delete ui;
}

int ElyByLoginDialog::exec()
{
    // Setup the login task and start it
    m_account = MinecraftAccount::createBlankElyBy();
    m_task = m_account->login();
    connect(m_task.get(), &Task::failed, this, &ElyByLoginDialog::onTaskFailed);
    connect(m_task.get(), &Task::succeeded, this, &QDialog::accept);
    connect(m_task.get(), &Task::aborted, this, &ElyByLoginDialog::reject);
    connect(m_task.get(), &Task::status, this, &ElyByLoginDialog::onTaskStatus);
    connect(m_task.get(), &AuthFlow::authorizeWithBrowser, this, &ElyByLoginDialog::authorizeWithBrowser);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_task.get(), &Task::abort);
    QMetaObject::invokeMethod(m_task.get(), &Task::start, Qt::QueuedConnection);

    return QDialog::exec();
}

void ElyByLoginDialog::onTaskFailed(QString reason)
{
    m_task->disconnect(this);
    ui->stackedWidget->setCurrentIndex(0);
    QString processed;
    for (const auto& line : reason.split('\n')) {
        if (line.size()) {
            processed += "<font color='red'>" + line.toHtmlEscaped() + "</font><br />";
        } else {
            processed += "<br />";
        }
    }
    ui->status->setText(processed);
    disconnect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_task.get(), &Task::abort);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &ElyByLoginDialog::reject);
}

void ElyByLoginDialog::onTaskStatus(QString status)
{
    if (ui->stackedWidget->currentIndex() == 0) {
        ui->status->setText(status.toHtmlEscaped());
    }
}

void ElyByLoginDialog::authorizeWithBrowser(QString url, QString code, [[maybe_unused]] int expiresIn)
{
    m_url = QUrl(url);
    ui->code->setText(code);
    const auto escapedUrl = url.toHtmlEscaped();
    ui->linkLabel->setText(
        tr("If the browser did not open, go to <a href=\"%1\">%2</a> and confirm the code above.").arg(escapedUrl, escapedUrl));
    ui->status->setText(tr("Log in with Ely.by in your browser to continue."));
    ui->stackedWidget->setCurrentIndex(1);
    adjustSize();

    if (!m_browserOpened) {
        m_browserOpened = true;
        openBrowser();
    }
}

void ElyByLoginDialog::openBrowser()
{
    // only open verification pages served over HTTPS
    if (!m_url.isValid() || m_url.scheme() != "https") {
        return;
    }
    if (!DesktopServices::openUrl(m_url)) {
        QApplication::clipboard()->setText(m_url.toString());
    }
}

// Public interface
MinecraftAccountPtr ElyByLoginDialog::newAccount(QWidget* parent)
{
    ElyByLoginDialog dlg(parent);
    if (dlg.exec() == QDialog::Accepted) {
        return dlg.m_account;
    }
    return nullptr;
}
