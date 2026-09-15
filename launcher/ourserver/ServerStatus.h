// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <expected>
#include <optional>

/**
 * Status of "Our Server", available without any server-side mods or plugins:
 *
 * - Server List Ping (TCP, the protocol of the multiplayer screen): online state, player count, slots, latency and a
 *   sample of the player names. Vanilla and Paper put at most 12 names into the sample.
 * - Query (UDP, needs enable-query=true in server.properties): the full list of player names.
 *
 * This file only contains the protocol, without network access, so it can be tested. See ServerStatusTask.h for the
 * network part.
 */
namespace ServerStatus {

//! a status response can contain a favicon, but is never anywhere near this size
inline constexpr qsizetype MAX_PACKET_LENGTH = 2 * 1024 * 1024;
inline constexpr qsizetype MAX_PLAYER_NAME_LENGTH = 16;

struct Players {
    int online = -1;  // -1 if the server did not report it
    int max = -1;     // -1 if the server did not report it
    QStringList names;

    //! players that are online but were not reported by name
    int unlistedCount() const;
};

struct Result {
    QString target;  // host:port that was checked
    bool online = false;
    Players players;
    qint64 latencyMs = -1;        // -1 if it could not be measured
    bool fullPlayerList = false;  // the names come from Query
    QString error;                // why the server is considered unavailable
    QString queryError;           // why Query failed, if it is configured
    QDateTime checkedAt;
};

//! a player name as accepted by vanilla and Paper servers: 1 to 16 printable ASCII characters without spaces
bool isValidPlayerName(const QString& name);

// Server List Ping

enum class ReadState { Complete, Incomplete, Invalid };

void writeVarInt(QByteArray& data, qint32 value);
//! reads a VarInt at offset and advances offset past it
ReadState readVarInt(const QByteArray& data, qsizetype& offset, qint32& value);

struct Packet {
    ReadState state = ReadState::Incomplete;
    qint32 id = -1;
    QByteArray payload;
    qsizetype size = 0;  // bytes taken from the buffer, including the length prefix
};

//! reads the first length-prefixed packet of a buffer
Packet readPacket(const QByteArray& buffer);

QByteArray handshakePacket(const QString& host, quint16 port);
QByteArray statusRequestPacket();
QByteArray pingRequestPacket(qint64 payload);

QString invalidStatusMessage();

//! payload of the status response packet (0x00)
std::expected<Players, QString> parseStatusResponse(const QByteArray& payload);
std::expected<Players, QString> parseStatusJson(const QJsonObject& status);
//! payload of the pong packet (0x01)
std::optional<qint64> parsePongPayload(const QByteArray& payload);

// Query

//! session IDs of Query only use the lower 4 bits of every byte
qint32 querySessionId(quint32 random);
QByteArray queryHandshakeRequest(qint32 sessionId);
//! returns the challenge token
std::expected<qint32, QString> parseQueryHandshakeResponse(const QByteArray& datagram, qint32 sessionId);
QByteArray queryFullStatRequest(qint32 sessionId, qint32 challengeToken);
std::expected<Players, QString> parseQueryFullStatResponse(const QByteArray& datagram, qint32 sessionId);

//! Server List Ping stays the source of the player count, Query only provides the names
Players mergePlayers(const Players& status, const std::optional<Players>& query);

}  // namespace ServerStatus
