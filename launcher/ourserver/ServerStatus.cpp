// SPDX-License-Identifier: GPL-3.0-only
#include "ServerStatus.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QtEndian>
#include <array>
#include <cmath>
#include <limits>

namespace ServerStatus {

namespace {
constexpr quint8 VARINT_VALUE_MASK = 0x7F;
constexpr quint8 VARINT_CONTINUE = 0x80;
constexpr int MAX_VARINT_BYTES = 5;

// see https://minecraft.wiki/w/Java_Edition_protocol/Server_List_Ping
constexpr qint32 PACKET_HANDSHAKE = 0x00;
constexpr qint32 PACKET_STATUS = 0x00;
constexpr qint32 PACKET_PING = 0x01;
constexpr qint32 PROTOCOL_VERSION_UNKNOWN = -1;  // convention when the version of the server is not known yet
constexpr qint32 NEXT_STATE_STATUS = 1;
constexpr qsizetype MAX_SAMPLE_ENTRIES = 1000;
constexpr auto NIL_UUID = "00000000-0000-0000-0000-000000000000";

// see https://minecraft.wiki/w/Query
constexpr quint8 QUERY_TYPE_HANDSHAKE = 0x09;
constexpr quint8 QUERY_TYPE_STAT = 0x00;
constexpr qsizetype QUERY_HEADER_SIZE = 5;  // type and session ID
constexpr qsizetype MAX_CHALLENGE_TOKEN_LENGTH = 11;

QString invalidQueryMessage()
{
    return QCoreApplication::translate("ServerStatus", "The server sent an invalid query response.");
}

void appendInt32(QByteArray& data, qint32 value)
{
    std::array<char, sizeof(qint32)> buffer{};
    qToBigEndian(value, buffer.data());
    data.append(buffer.data(), buffer.size());
}

std::optional<qint32> int32At(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + static_cast<qsizetype>(sizeof(qint32)) > data.size()) {
        return std::nullopt;
    }
    return qFromBigEndian<qint32>(data.constData() + offset);
}

//! reads a null-terminated string at offset and advances offset past the terminator
std::optional<QByteArray> readCString(const QByteArray& data, qsizetype& offset)
{
    const auto end = data.indexOf('\0', offset);
    if (end < 0) {
        return std::nullopt;
    }
    auto value = data.mid(offset, end - offset);
    offset = end + 1;
    return value;
}

std::optional<int> readCount(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (number < 0 || number > std::numeric_limits<int>::max() || number != std::floor(number)) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

void addPlayerName(Players& players, QSet<QString>& seen, const QString& name)
{
    if (isValidPlayerName(name) && !seen.contains(name)) {
        seen.insert(name);
        players.names.append(name);
    }
}

QByteArray framePacket(qint32 id, const QByteArray& payload)
{
    QByteArray body;
    writeVarInt(body, id);
    body.append(payload);

    QByteArray packet;
    writeVarInt(packet, static_cast<qint32>(body.size()));
    packet.append(body);
    return packet;
}

QByteArray queryPacket(quint8 type, qint32 sessionId)
{
    QByteArray data = QByteArray::fromHex("fefd");
    data.append(static_cast<char>(type));
    appendInt32(data, sessionId);
    return data;
}

bool hasQueryHeader(const QByteArray& datagram, quint8 type, qint32 sessionId)
{
    return datagram.size() >= QUERY_HEADER_SIZE && static_cast<quint8>(datagram.at(0)) == type && int32At(datagram, 1) == sessionId;
}
}  // namespace

int Players::unlistedCount() const
{
    return online < 0 ? 0 : qMax(0, online - static_cast<int>(names.size()));
}

bool isValidPlayerName(const QString& name)
{
    if (name.isEmpty() || name.size() > MAX_PLAYER_NAME_LENGTH) {
        return false;
    }
    for (const QChar c : name) {
        if (c.unicode() <= 0x20 || c.unicode() >= 0x7f) {
            return false;
        }
    }
    return true;
}

void writeVarInt(QByteArray& data, qint32 value)
{
    // negative values use all 5 bytes, so shift the unsigned representation
    auto remaining = static_cast<quint32>(value);
    while ((remaining & ~static_cast<quint32>(VARINT_VALUE_MASK)) != 0) {
        data.append(static_cast<char>((remaining & VARINT_VALUE_MASK) | VARINT_CONTINUE));
        remaining >>= 7;
    }
    data.append(static_cast<char>(remaining));
}

ReadState readVarInt(const QByteArray& data, qsizetype& offset, qint32& value)
{
    quint32 result = 0;
    for (int i = 0; i < MAX_VARINT_BYTES; i++) {
        if (offset + i >= data.size()) {
            return ReadState::Incomplete;
        }
        const auto byte = static_cast<quint8>(data.at(offset + i));
        result |= static_cast<quint32>(byte & VARINT_VALUE_MASK) << (7 * i);
        if ((byte & VARINT_CONTINUE) == 0) {
            offset += i + 1;
            value = static_cast<qint32>(result);
            return ReadState::Complete;
        }
    }
    return ReadState::Invalid;
}

Packet readPacket(const QByteArray& buffer)
{
    Packet packet;
    qsizetype offset = 0;
    qint32 length = 0;
    packet.state = readVarInt(buffer, offset, length);
    if (packet.state != ReadState::Complete) {
        return packet;
    }
    if (length <= 0 || length > MAX_PACKET_LENGTH) {
        packet.state = ReadState::Invalid;
        return packet;
    }
    if (buffer.size() - offset < length) {
        packet.state = ReadState::Incomplete;
        return packet;
    }

    const auto body = buffer.mid(offset, length);
    qsizetype bodyOffset = 0;
    if (readVarInt(body, bodyOffset, packet.id) != ReadState::Complete) {
        packet.state = ReadState::Invalid;
        return packet;
    }
    packet.payload = body.mid(bodyOffset);
    packet.size = offset + length;
    packet.state = ReadState::Complete;
    return packet;
}

QByteArray handshakePacket(const QString& host, quint16 port)
{
    QByteArray payload;
    writeVarInt(payload, PROTOCOL_VERSION_UNKNOWN);
    const auto hostData = host.toUtf8();
    writeVarInt(payload, static_cast<qint32>(hostData.size()));
    payload.append(hostData);
    std::array<char, sizeof(quint16)> portData{};
    qToBigEndian(port, portData.data());
    payload.append(portData.data(), portData.size());
    writeVarInt(payload, NEXT_STATE_STATUS);
    return framePacket(PACKET_HANDSHAKE, payload);
}

QByteArray statusRequestPacket()
{
    return framePacket(PACKET_STATUS, {});
}

QByteArray pingRequestPacket(qint64 payload)
{
    std::array<char, sizeof(qint64)> data{};
    qToBigEndian(payload, data.data());
    return framePacket(PACKET_PING, QByteArray(data.data(), data.size()));
}

QString invalidStatusMessage()
{
    return QCoreApplication::translate("ServerStatus", "The server sent an invalid status response.");
}

std::expected<Players, QString> parseStatusResponse(const QByteArray& payload)
{
    qsizetype offset = 0;
    qint32 length = 0;
    if (readVarInt(payload, offset, length) != ReadState::Complete || length <= 0 || payload.size() - offset < length) {
        return std::unexpected(invalidStatusMessage());
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(payload.mid(offset, length), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(invalidStatusMessage());
    }
    return parseStatusJson(doc.object());
}

std::expected<Players, QString> parseStatusJson(const QJsonObject& status)
{
    Players players;
    const auto playersValue = status.value("players");
    if (playersValue.isUndefined()) {
        // the server does not report its players at all
        return players;
    }
    if (!playersValue.isObject()) {
        return std::unexpected(invalidStatusMessage());
    }
    const auto playersObject = playersValue.toObject();
    const auto online = readCount(playersObject.value("online"));
    const auto max = readCount(playersObject.value("max"));
    if (!online || !max) {
        return std::unexpected(invalidStatusMessage());
    }
    players.online = *online;
    players.max = *max;

    QSet<QString> seen;
    const auto sample = playersObject.value("sample").toArray();
    for (qsizetype i = 0; i < qMin(sample.size(), MAX_SAMPLE_ENTRIES); i++) {
        const auto entry = sample.at(i).toObject();
        // placeholders such as "Anonymous Player" for players that hide themselves use the nil UUID
        if (entry.value("id").toString() == NIL_UUID) {
            continue;
        }
        addPlayerName(players, seen, entry.value("name").toString());
    }
    return players;
}

std::optional<qint64> parsePongPayload(const QByteArray& payload)
{
    if (payload.size() != sizeof(qint64)) {
        return std::nullopt;
    }
    return qFromBigEndian<qint64>(payload.constData());
}

qint32 querySessionId(quint32 random)
{
    return static_cast<qint32>(random & 0x0F0F0F0FU);
}

QByteArray queryHandshakeRequest(qint32 sessionId)
{
    return queryPacket(QUERY_TYPE_HANDSHAKE, sessionId);
}

std::expected<qint32, QString> parseQueryHandshakeResponse(const QByteArray& datagram, qint32 sessionId)
{
    if (!hasQueryHeader(datagram, QUERY_TYPE_HANDSHAKE, sessionId)) {
        return std::unexpected(invalidQueryMessage());
    }
    qsizetype offset = QUERY_HEADER_SIZE;
    const auto token = readCString(datagram, offset);
    if (!token || token->isEmpty() || token->size() > MAX_CHALLENGE_TOKEN_LENGTH) {
        return std::unexpected(invalidQueryMessage());
    }
    bool ok = false;
    const qint64 value = token->toLongLong(&ok);
    if (!ok || value < std::numeric_limits<qint32>::min() || value > std::numeric_limits<quint32>::max()) {
        return std::unexpected(invalidQueryMessage());
    }
    return static_cast<qint32>(static_cast<quint32>(value));
}

QByteArray queryFullStatRequest(qint32 sessionId, qint32 challengeToken)
{
    QByteArray data = queryPacket(QUERY_TYPE_STAT, sessionId);
    appendInt32(data, challengeToken);
    // the padding asks for the full stat instead of the basic one
    data.append(4, '\0');
    return data;
}

std::expected<Players, QString> parseQueryFullStatResponse(const QByteArray& datagram, qint32 sessionId)
{
    if (!hasQueryHeader(datagram, QUERY_TYPE_STAT, sessionId)) {
        return std::unexpected(invalidQueryMessage());
    }
    qsizetype offset = QUERY_HEADER_SIZE;

    // "splitnum" 0x00 0x80 0x00
    static const QByteArray s_statHeader = QByteArray::fromHex("73706c69746e756d008000");
    if (datagram.mid(offset, s_statHeader.size()) != s_statHeader) {
        return std::unexpected(invalidQueryMessage());
    }
    offset += s_statHeader.size();

    // key/value section, terminated by an empty key
    QHash<QByteArray, QByteArray> values;
    while (true) {
        const auto key = readCString(datagram, offset);
        if (!key) {
            return std::unexpected(invalidQueryMessage());
        }
        if (key->isEmpty()) {
            break;
        }
        const auto value = readCString(datagram, offset);
        if (!value) {
            return std::unexpected(invalidQueryMessage());
        }
        values.insert(*key, *value);
    }

    // 0x01 "player_" 0x00 0x00
    static const QByteArray s_playersHeader = QByteArray::fromHex("01706c617965725f0000");
    if (datagram.mid(offset, s_playersHeader.size()) != s_playersHeader) {
        return std::unexpected(invalidQueryMessage());
    }
    offset += s_playersHeader.size();

    Players players;
    bool onlineOk = false;
    bool maxOk = false;
    players.online = values.value("numplayers").toInt(&onlineOk);
    players.max = values.value("maxplayers").toInt(&maxOk);
    if (!onlineOk || !maxOk || players.online < 0 || players.max < 0) {
        return std::unexpected(invalidQueryMessage());
    }

    // player names, terminated by an empty name
    QSet<QString> seen;
    while (offset < datagram.size()) {
        const auto name = readCString(datagram, offset);
        if (!name || name->isEmpty()) {
            break;
        }
        addPlayerName(players, seen, QString::fromUtf8(*name));
    }
    return players;
}

Players mergePlayers(const Players& status, const std::optional<Players>& query)
{
    if (!query) {
        return status;
    }
    Players merged = status;
    merged.names = query->names;
    if (merged.online < 0) {
        merged.online = query->online;
    }
    if (merged.max < 0) {
        merged.max = query->max;
    }
    return merged;
}

}  // namespace ServerStatus
