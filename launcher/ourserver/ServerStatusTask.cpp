// SPDX-License-Identifier: GPL-3.0-only
#include "ServerStatusTask.h"

#include <QDateTime>
#include <QHostInfo>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUdpSocket>
#include <utility>

#include "ourserver/ServerPackLogging.h"
#include "ourserver/ServerPackManifest.h"
#include "ui/pages/instance/McResolver.h"

namespace {
constexpr int RESOLVE_TIMEOUT_MS = 15000;
// some DNS servers need 10 seconds and more to answer that a domain has no SRV record, don't wait for them
constexpr int SRV_LOOKUP_TIMEOUT_MS = 3000;
constexpr int STATUS_TIMEOUT_MS = 10000;  // connecting and the status response
constexpr int PING_TIMEOUT_MS = 3000;
constexpr int QUERY_TIMEOUT_MS = 3000;
constexpr qint32 PACKET_STATUS_RESPONSE = 0x00;
constexpr qint32 PACKET_PONG = 0x01;
}  // namespace

ServerStatusTask::ServerStatusTask(QString host, quint16 port, quint16 queryPort)
    : m_host(std::move(host)), m_port(port), m_queryPort(queryPort)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ServerStatusTask::onTimeout);
    m_result.target = m_port == ServerPack::DEFAULT_SERVER_PORT ? m_host : QString("%1:%2").arg(m_host).arg(m_port);
}

bool ServerStatusTask::abort()
{
    if (isRunning()) {
        m_stage = Stage::Done;
        m_timeout.stop();
        closeSockets();
        emitAborted();
    }
    return true;
}

void ServerStatusTask::executeTask()
{
    setStatus(tr("Checking the server status..."));
    m_stage = Stage::Resolving;
    m_timeout.start(RESOLVE_TIMEOUT_MS);

    // an IP address needs no DNS lookup
    if (QHostAddress address; address.setAddress(m_host)) {
        connectToServer(address, m_port);
        return;
    }

    auto* resolver = new McResolver(this, m_host, m_port);
    connect(resolver, &McResolver::succeeded, this, [this](const QString& ip, int port) {
        if (m_stage == Stage::Resolving) {
            connectToServer(QHostAddress(ip), static_cast<quint16>(port));
        }
    });
    connect(resolver, &McResolver::failed, this, [this](const QString& error) {
        if (m_stage == Stage::Resolving) {
            qCDebug(serverPackLogC).noquote() << "[ServerStatus] Lookup of" << m_host << "failed:" << error;
            markUnavailable(tr("The server address could not be resolved."));
        }
    });
    connect(resolver, &McResolver::finished, resolver, &QObject::deleteLater);
    resolver->ping();

    QTimer::singleShot(SRV_LOOKUP_TIMEOUT_MS, this, [this] {
        if (m_stage != Stage::Resolving) {
            return;
        }
        qCDebug(serverPackLogC).noquote() << "[ServerStatus] SRV lookup of" << m_host << "is slow, looking up the address directly";
        QHostInfo::lookupHost(m_host, this, [this](const QHostInfo& hostInfo) {
            if (m_stage != Stage::Resolving) {
                return;
            }
            if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
                markUnavailable(tr("The server address could not be resolved."));
                return;
            }
            connectToServer(hostInfo.addresses().constFirst(), m_port);
        });
    });
}

void ServerStatusTask::connectToServer(const QHostAddress& address, quint16 port)
{
    m_stage = Stage::Status;
    m_timeout.start(STATUS_TIMEOUT_MS);
    m_address = address;
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this, port] {
        m_socket->write(ServerStatus::handshakePacket(m_host, port));
        m_socket->write(ServerStatus::statusRequestPacket());
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &ServerStatusTask::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ServerStatusTask::onSocketError);
    m_socket->connectToHost(address, port);
}

void ServerStatusTask::onSocketReadyRead()
{
    m_buffer.append(m_socket->readAll());
    while (m_stage == Stage::Status || m_stage == Stage::Ping) {
        const auto packet = ServerStatus::readPacket(m_buffer);
        if (packet.state == ServerStatus::ReadState::Incomplete) {
            return;
        }
        if (packet.state == ServerStatus::ReadState::Invalid) {
            if (m_stage == Stage::Status) {
                markUnavailable(ServerStatus::invalidStatusMessage());
            } else {
                finishPing();
            }
            return;
        }
        m_buffer.remove(0, packet.size);
        if (m_stage == Stage::Status) {
            onStatusPacket(packet);
        } else {
            onPongPacket(packet);
        }
    }
}

void ServerStatusTask::onSocketError()
{
    if (m_stage == Stage::Status) {
        markUnavailable(tr("Could not connect to the server: %1").arg(m_socket->errorString()));
    } else if (m_stage == Stage::Ping) {
        // the status is already known, only the latency stays unknown
        finishPing();
    }
}

void ServerStatusTask::onStatusPacket(const ServerStatus::Packet& packet)
{
    if (packet.id != PACKET_STATUS_RESPONSE) {
        markUnavailable(ServerStatus::invalidStatusMessage());
        return;
    }
    const auto players = ServerStatus::parseStatusResponse(packet.payload);
    if (!players) {
        markUnavailable(players.error());
        return;
    }
    m_result.online = true;
    m_statusPlayers = *players;
    m_result.players = *players;

    m_stage = Stage::Ping;
    m_pingPayload = QDateTime::currentMSecsSinceEpoch();
    m_timeout.start(PING_TIMEOUT_MS);
    m_pingTimer.start();
    m_socket->write(ServerStatus::pingRequestPacket(m_pingPayload));
}

void ServerStatusTask::onPongPacket(const ServerStatus::Packet& packet)
{
    const auto payload = packet.id == PACKET_PONG ? ServerStatus::parsePongPayload(packet.payload) : std::nullopt;
    if (payload == m_pingPayload) {
        m_result.latencyMs = m_pingTimer.elapsed();
    }
    finishPing();
}

void ServerStatusTask::finishPing()
{
    if (m_stage != Stage::Ping) {
        return;
    }
    m_timeout.stop();
    closeSockets();
    if (m_queryPort != 0) {
        startQuery();
    } else {
        finish();
    }
}

void ServerStatusTask::startQuery()
{
    m_stage = Stage::QueryHandshake;
    m_querySessionId = ServerStatus::querySessionId(QRandomGenerator::global()->generate());
    m_querySocket = new QUdpSocket(this);
    connect(m_querySocket, &QUdpSocket::readyRead, this, &ServerStatusTask::onQueryReadyRead);
    connect(m_querySocket, &QUdpSocket::errorOccurred, this,
            [this] { failQuery(tr("The query port is not reachable: %1").arg(m_querySocket->errorString())); });
    m_timeout.start(QUERY_TIMEOUT_MS);
    if (m_querySocket->writeDatagram(ServerStatus::queryHandshakeRequest(m_querySessionId), m_address, m_queryPort) < 0) {
        failQuery(tr("The query port is not reachable: %1").arg(m_querySocket->errorString()));
    }
}

void ServerStatusTask::onQueryReadyRead()
{
    while (m_querySocket && m_querySocket->hasPendingDatagrams()) {
        const auto datagram = m_querySocket->receiveDatagram().data();
        if (m_stage == Stage::QueryHandshake) {
            const auto token = ServerStatus::parseQueryHandshakeResponse(datagram, m_querySessionId);
            if (!token) {
                failQuery(token.error());
                return;
            }
            m_stage = Stage::QueryStat;
            m_timeout.start(QUERY_TIMEOUT_MS);
            if (m_querySocket->writeDatagram(ServerStatus::queryFullStatRequest(m_querySessionId, *token), m_address, m_queryPort) < 0) {
                failQuery(tr("The query port is not reachable: %1").arg(m_querySocket->errorString()));
                return;
            }
        } else if (m_stage == Stage::QueryStat) {
            const auto players = ServerStatus::parseQueryFullStatResponse(datagram, m_querySessionId);
            if (!players) {
                failQuery(players.error());
                return;
            }
            m_result.players = ServerStatus::mergePlayers(m_statusPlayers, *players);
            m_result.fullPlayerList = true;
            finish();
            return;
        } else {
            return;
        }
    }
}

void ServerStatusTask::failQuery(const QString& reason)
{
    if (m_stage != Stage::QueryHandshake && m_stage != Stage::QueryStat) {
        return;
    }
    qCDebug(serverPackLogC).noquote() << "[ServerStatus] Query of" << m_result.target << "failed:" << reason;
    m_result.queryError = reason;
    finish();
}

void ServerStatusTask::onTimeout()
{
    switch (m_stage) {
        case Stage::Resolving:
        case Stage::Status:
            markUnavailable(tr("The server did not respond in time."));
            break;
        case Stage::Ping:
            finishPing();
            break;
        case Stage::QueryHandshake:
        case Stage::QueryStat:
            failQuery(tr("The query port did not respond in time."));
            break;
        case Stage::Done:
            break;
    }
}

void ServerStatusTask::markUnavailable(const QString& reason)
{
    m_result.online = false;
    m_result.players = {};
    m_result.latencyMs = -1;
    m_result.fullPlayerList = false;
    m_result.error = reason;
    finish();
}

void ServerStatusTask::finish()
{
    if (m_stage == Stage::Done) {
        return;
    }
    m_stage = Stage::Done;
    m_timeout.stop();
    closeSockets();
    m_result.checkedAt = QDateTime::currentDateTime();
    qCDebug(serverPackLogC).noquote().nospace()
        << "[ServerStatus] " << m_result.target << ": " << (m_result.online ? "online" : "unavailable") << ", players "
        << m_result.players.online << "/" << m_result.players.max << ", latency " << m_result.latencyMs << " ms";
    emitSucceeded();
}

void ServerStatusTask::closeSockets()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_querySocket) {
        m_querySocket->disconnect(this);
        m_querySocket->close();
        m_querySocket->deleteLater();
        m_querySocket = nullptr;
    }
}
