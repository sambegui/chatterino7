#include "providers/twitch/TwitchPrediction.hpp"

#include <QJsonArray>

namespace {

using namespace chatterino;

/// Twitch colours outcomes by position when the event does not name one.
QString colorForPosition(int index, int outcomeCount)
{
    if (outcomeCount == 2)
    {
        return index == 0 ? QStringLiteral("BLUE") : QStringLiteral("PINK");
    }

    switch (index)
    {
        case 0:
            return QStringLiteral("BLUE");
        case 1:
            return QStringLiteral("PINK");
        case 2:
            return QStringLiteral("GREEN");
        default:
            return QStringLiteral("BLUE");
    }
}

}  // namespace

namespace chatterino {

std::optional<TwitchPrediction> parseTwitchPrediction(
    const QJsonObject &payload)
{
    auto data = payload.value("data").toObject();

    auto body = data.value("event").toObject();
    if (body.isEmpty())
    {
        body = data;
    }
    if (body.isEmpty())
    {
        return std::nullopt;
    }

    TwitchPrediction prediction;
    prediction.id = body.value("id").toString();
    prediction.title = body.value("title").toString();
    prediction.status = body.value("status").toString();
    prediction.windowSeconds = body.value("prediction_window_seconds").toInt();

    prediction.winningOutcomeId = body.value("winning_outcome_id").toString();
    if (prediction.winningOutcomeId.isEmpty())
    {
        prediction.winningOutcomeId = body.value("winningOutcomeId").toString();
    }

    auto createdAt = body.value("created_at").toString();
    if (!createdAt.isEmpty())
    {
        prediction.createdAt = QDateTime::fromString(createdAt, Qt::ISODate);
    }

    auto outcomes = body.value("outcomes").toArray();
    prediction.outcomes.reserve(static_cast<size_t>(outcomes.size()));
    for (int i = 0; i < outcomes.size(); i++)
    {
        auto object = outcomes.at(i).toObject();

        TwitchPredictionOutcome outcome;
        outcome.id = object.value("id").toString();
        outcome.title = object.value("title").toString();
        outcome.points = object.value("total_points").toInteger();
        outcome.users = object.value("total_users").toInt();

        outcome.color = object.value("color").toString();
        if (outcome.color.isEmpty())
        {
            outcome.color = colorForPosition(i, outcomes.size());
        }

        prediction.totalPoints += outcome.points;
        prediction.outcomes.push_back(std::move(outcome));
    }

    if (prediction.id.isEmpty() || prediction.title.isEmpty() ||
        prediction.outcomes.empty())
    {
        return std::nullopt;
    }

    return prediction;
}

}  // namespace chatterino
