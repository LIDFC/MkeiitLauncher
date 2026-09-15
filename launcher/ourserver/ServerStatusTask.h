// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QString>
#include <QTimer>

#include "QObjectPtr.h"
#include "ourserver/ServerStatus.h"
#include "tasks/Task.h"

class QTcpSocket;
class QUdpSocket;

/**
 * Checks the status of "Our Server":
 *
 * 1. resolves the address like Minecraft does (SRV record, then A/AAAA)
 * 2. asks for the status with Server List Ping and measures the latency with its ping packet
 * 3. if a query port is configured, asks Query for the full player list
 *
 * An unreachable server is a regular result (Result::online is false), the task only fails when it is aborted. Nothing
 * is guessed: values the server did not send stay unknown.
 */
class ServerStatusTask : public Task {
    Q_OBJECT
   public:
    using Ptr = shared_qobject_ptr<ServerStatusTask>;

    //! queryPort 0 disables Query
    ServerStatusTask(QString host, quint16 port, quint16 queryPort);
    ~ServerStatusTask() override = default;

    const ServerStatus::Result& result() const { return m_result; }

    bool canAbort() const override { return true; }

   public slots:
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    enum class Stage { Resolving, Status, Ping, QueryHandshake, QueryStat, Done };

    void connectToServer(const QHostAddress& address, quint16 port);
    void onSocketReadyRead();
    void onSocketError();
    void onStatusPacket(const ServerStatus::Packet& packet);
    void onPongPacket(const ServerStatus::Packet& packet);
    void finishPing();

    void startQuery();
    void onQueryReadyRead();
    void failQuery(const QString& reason);

    void onTimeout();
    void markUnavailable(const QString& reason);
    void finish();
    void closeSockets();

    QString m_host;
    quint16 m_port;
    quint16 m_queryPort;

    Stage m_stage = Stage::Resolving;
    QHostAddress m_address;
    QTcpSocket* m_socket = nullptr;
    QUdpSocket* m_querySocket = nullptr;
    QTimer m_timeout;
    QElapsedTimer m_pingTimer;
    QByteArray m_buffer;
    qint64 m_pingPayload = 0;
    qint32 m_querySessionId = 0;
    ServerStatus::Players m_statusPlayers;

    ServerStatus::Result m_result;
};
