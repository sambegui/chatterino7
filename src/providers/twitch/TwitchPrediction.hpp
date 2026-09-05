#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino {

struct TwitchPredictionOutcome {
    QString id;
    QString title;
    qint64 points = 0;
    int users = 0;
    /// BLUE, PINK or GREEN. Twitch assigns these by position when it does not
    /// send one.
    QString color;
};

/// A prediction as it is shown to a viewer. Creating, locking and resolving
/// need the private API and are not part of this.
struct TwitchPrediction {
    QString id;
    QString title;
    /// ACTIVE, LOCKED, RESOLVED or CANCELED.
    QString status;
    std::vector<TwitchPredictionOutcome> outcomes;
    QString winningOutcomeId;
    QDateTime createdAt;
    /// How long the prediction accepts entries for, from createdAt.
    int windowSeconds = 0;
    qint64 totalPoints = 0;

    [[nodiscard]] bool isActive() const
    {
        return this->status == "ACTIVE";
    }

    /// When entries close, or an invalid time if that cannot be worked out.
    [[nodiscard]] QDateTime locksAt() const
    {
        if (!this->createdAt.isValid() || this->windowSeconds <= 0)
        {
            return {};
        }
        return this->createdAt.addSecs(this->windowSeconds);
    }
};

/// Parses a predictions-channel-v1.{channelID} event, as {type, topic, data}.
std::optional<TwitchPrediction> parseTwitchPrediction(
    const QJsonObject &payload);

}  // namespace chatterino
