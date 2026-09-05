#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace chatterino {

/// Badge information for a Kick user
struct KickBadge {
    QString type;  // "broadcaster", "moderator", "subscriber", "vip", etc.
    QString text;  // Display text
    int count{0};  // For subscriber badges (months)

    static KickBadge fromJson(const QJsonObject &json);
};

/// User identity information from Kick
struct KickIdentity {
    QString color;  // Hex color (e.g., "#FF5733")
    std::vector<KickBadge> badges;

    static KickIdentity fromJson(const QJsonObject &json);
};

/// Sender information for a Kick message
struct KickSender {
    int id{0};
    QString username;
    QString slug;
    KickIdentity identity;

    static KickSender fromJson(const QJsonObject &json);
};

/// Emote position in message content
struct KickEmotePosition {
    int start{0};  // Start index in content
    int end{0};    // End index in content
};

/// Emote information from Kick message
struct KickEmote {
    QString emoteId;
    QString emoteName;  // Emote display name (if provided by API)
    std::vector<KickEmotePosition> positions;

    static KickEmote fromJson(const QJsonObject &json);
};

/// Raw Kick message structure (maps to Pusher WebSocket event payload)
struct KickMessage {
    QString id;  // Message ID (ULID format)
    int chatroomId{0};
    QString content;  // Message text
    QString type;     // "message", "subscription", "gifted-subscriptions", etc.
    QDateTime createdAt;  // ISO 8601 timestamp
    KickSender sender;
    std::vector<KickEmote> emotes;  // Emotes with positions

    /// Parse a KickMessage from a Pusher event data JSON object
    /// @param json The parsed JSON object from the Pusher "data" field
    /// @return A KickMessage populated from the JSON
    static KickMessage fromJson(const QJsonObject &json);

    /// Check if this message is a regular chat message
    [[nodiscard]] bool isChatMessage() const;
};

}  // namespace chatterino
