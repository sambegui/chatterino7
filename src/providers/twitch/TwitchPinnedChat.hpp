#pragma once

#include <QDateTime>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <optional>

namespace chatterino {

struct TwitchPinnedMessage {
    QString pinId;
    QString messageId;
    QString text;
    QString authorName;
    QString authorLogin;
    /// Hex chat colour, may be empty.
    QString authorColor;
    QString pinnerName;
    QDateTime pinnedAt;
    /// Invalid when the pin has no expiry.
    QDateTime endsAt;
};

std::optional<TwitchPinnedMessage> parseTwitchPinnedChat(
    const QJsonValue &response);

/// Fetches the message currently pinned in a channel.
///
/// Twitch exposes no public API for this, so this is an anonymous persisted
/// query against gql.twitch.tv. It sends no token and is not tied to the user's
/// account. The PubSub pinned-chat topic only says that a pin changed, so this
/// is what actually reads the content.
void fetchTwitchPinnedChat(
    const QString &channelId,
    std::function<void(std::optional<TwitchPinnedMessage>)> onSuccess,
    std::function<void(const QString &)> onError);

}  // namespace chatterino
