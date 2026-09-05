#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino {

struct TwitchPollChoice {
    QString id;
    QString title;
    int votes = 0;
};

/// A poll as it is shown to a viewer. Creating or voting needs far more than
/// this and is not something the public API allows.
struct TwitchPoll {
    QString id;
    QString title;
    /// ACTIVE, COMPLETED, TERMINATED or ARCHIVED.
    QString status;
    std::vector<TwitchPollChoice> choices;
    int totalVotes = 0;
    /// Invalid when Twitch did not say when the poll closes.
    QDateTime endsAt;

    [[nodiscard]] bool isActive() const
    {
        return this->status == "ACTIVE";
    }
};

/// Parses a polls.{channelID} event, as {type, topic, data}.
///
/// Returns nothing for a payload that carries no usable poll. A POLL_END with
/// no poll body means "whatever was running has finished", which the caller
/// handles by clearing the current poll.
std::optional<TwitchPoll> parseTwitchPoll(const QJsonObject &payload);

}  // namespace chatterino
