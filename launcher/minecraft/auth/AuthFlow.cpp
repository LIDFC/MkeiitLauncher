#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/steps/ElyByDeviceCodeStep.h"
#include "minecraft/auth/steps/ElyByProfileStep.h"
#include "minecraft/auth/steps/ElyByRefreshStep.h"
#include "minecraft/auth/steps/GetSkinStep.h"
#include "tasks/Task.h"

#include "AuthFlow.h"

AuthFlow::AuthFlow(AccountData* data, Action action) : Task(), m_data(data)
{
    // Offline accounts have nothing to authenticate. Legacy Microsoft accounts are rejected in executeTask().
    if (data->type == AccountType::ElyBy) {
        if (action == Action::Login) {
            auto oauthStep = makeShared<ElyByDeviceCodeStep>(m_data);
            connect(oauthStep.get(), &ElyByDeviceCodeStep::authorizeWithBrowser, this, &AuthFlow::authorizeWithBrowser);
            m_steps.append(oauthStep);
        } else {
            m_steps.append(makeShared<ElyByRefreshStep>(m_data));
        }
        m_steps.append(makeShared<ElyByProfileStep>(m_data));
        m_steps.append(makeShared<GetSkinStep>(m_data));
    }
    changeState(AccountTaskState::STATE_CREATED);
}

void AuthFlow::succeed()
{
    m_data->validity_ = Validity::Certain;
    changeState(AccountTaskState::STATE_SUCCEEDED, tr("Finished all authentication steps"));
}

void AuthFlow::executeTask()
{
    if (m_data->type == AccountType::MSA) {
        changeState(AccountTaskState::STATE_DISABLED,
                    tr("Microsoft accounts are no longer supported. Please remove this account and add a Guest / Offline or Ely.by "
                       "account instead."));
        return;
    }
    changeState(AccountTaskState::STATE_WORKING, tr("Initializing"));
    nextStep();
}

void AuthFlow::nextStep()
{
    if (!Task::isRunning()) {
        return;
    }
    if (m_steps.size() == 0) {
        // we got to the end without an incident... assume this is all.
        m_currentStep.reset();
        succeed();
        return;
    }
    m_currentStep = m_steps.front();
    qDebug() << "AuthFlow:" << m_currentStep->describe();
    setStatus(m_currentStep->describe());
    m_steps.pop_front();
    connect(m_currentStep.get(), &AuthStep::finished, this, &AuthFlow::stepFinished);

    m_currentStep->perform();
}

void AuthFlow::stepFinished(AccountTaskState resultingState, QString message)
{
    if (changeState(resultingState, message))
        nextStep();
}

bool AuthFlow::changeState(AccountTaskState newState, QString reason)
{
    m_taskState = newState;
    setDetails(reason);
    switch (newState) {
        case AccountTaskState::STATE_CREATED: {
            setStatus(tr("Waiting..."));
            m_data->errorString.clear();
            return true;
        }
        case AccountTaskState::STATE_WORKING: {
            if (!m_currentStep) {
                setStatus(tr("Preparing to log in..."));
            }
            m_data->accountState = AccountState::Working;
            return true;
        }
        case AccountTaskState::STATE_SUCCEEDED: {
            setStatus(tr("Authentication task succeeded."));
            m_data->accountState = AccountState::Online;
            emitSucceeded();
            return false;
        }
        case AccountTaskState::STATE_OFFLINE: {
            setStatus(tr("Failed to contact the authentication server."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Offline;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_DISABLED: {
            setStatus(tr("This account cannot be used anymore. It needs to be added again."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Disabled;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_SOFT: {
            setStatus(tr("Encountered an error during authentication."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Errored;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_HARD: {
            setStatus(tr("Failed to authenticate. The session has expired."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Expired;
            emitFailed(reason);
            return false;
        }
        case AccountTaskState::STATE_FAILED_GONE: {
            setStatus(tr("Failed to authenticate. The account no longer exists."));
            m_data->errorString = reason;
            m_data->accountState = AccountState::Gone;
            emitFailed(reason);
            return false;
        }
        default: {
            setStatus(tr("..."));
            QString error = tr("Unknown account task state: %1").arg(int(newState));
            m_data->accountState = AccountState::Errored;
            emitFailed(error);
            return false;
        }
    }
}
bool AuthFlow::abort()
{
    if (m_currentStep)
        m_currentStep->abort();
    emitAborted();
    return true;
}
