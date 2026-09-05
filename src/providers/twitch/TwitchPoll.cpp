#include "providers/twitch/TwitchPoll.hpp"

#include <QJsonArray>

namespace {

using namespace chatterino;

/// PubSub is inconsistent about casing between events, so try both spellings.
QJsonValue anyOf(const QJsonObject &object, const char *snakeCase,
                 const char *camelCase)
{
    auto value = object.value(snakeCase);
    if (value.isUndefined() || value.isNull())
    {
        return object.value(camelCase);
    }
    return value;
}

QDateTime parseTimestamp(const QJsonValue &value)
{
    auto text = value.toString();
    if (text.isEmpty())
    {
        return {};
    }
    return QDateTime::fromString(text, Qt::ISODate);
}

}  // namespace

namespace chatterino {

std::optional<TwitchPoll> parseTwitchPoll(const QJsonObject &payload)
{
    auto data = payload.value("data").toObject();

    // some events nest the poll, others are the poll
    auto body = data.value("poll").toObject();
    if (body.isEmpty())
    {
        body = data;
    }
    if (body.isEmpty())
    {
        return std::nullopt;
    }

    TwitchPoll poll;
    poll.id = anyOf(body, "poll_id", "id").toString();
    poll.title = body.value("title").toString();

    poll.status = body.value("status").toString();
    if (poll.status.isEmpty())
    {
        poll.status = payload.value("type").toString() == "POLL_END"
                          ? QStringLiteral("COMPLETED")
                          : QStringLiteral("ACTIVE");
    }

    poll.endsAt = parseTimestamp(anyOf(body, "ends_at", "endsAt"));

    auto choices = body.value("choices").toArray();
    poll.choices.reserve(static_cast<size_t>(choices.size()));
    for (const auto &value : choices)
    {
        auto object = value.toObject();

        TwitchPollChoice choice;
        choice.id = anyOf(object, "choice_id", "id").toString();
        choice.title = object.value("title").toString();
        choice.votes = object.value("votes").toObject().value("total").toInt();

        poll.totalVotes += choice.votes;
        poll.choices.push_back(std::move(choice));
    }

    if (poll.id.isEmpty() || poll.title.isEmpty() || poll.choices.empty())
    {
        return std::nullopt;
    }

    if (payload.value("type").toString() == "POLL_END" &&
        !poll.endsAt.isValid())
    {
        poll.endsAt = QDateTime::currentDateTimeUtc();
    }

    return poll;
}

}  // namespace chatterino
