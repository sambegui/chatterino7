#include "providers/kick/KickMessage.hpp"

#include <QJsonArray>

namespace chatterino {

KickBadge KickBadge::fromJson(const QJsonObject &json)
{
    KickBadge badge;
    badge.type = json["type"].toString();
    badge.text = json["text"].toString();
    badge.count = json["count"].toInt(0);
    return badge;
}

KickIdentity KickIdentity::fromJson(const QJsonObject &json)
{
    KickIdentity identity;
    identity.color = json["color"].toString();

    const auto badgesArray = json["badges"].toArray();
    identity.badges.reserve(badgesArray.size());
    for (const auto &badgeVal : badgesArray)
    {
        identity.badges.push_back(KickBadge::fromJson(badgeVal.toObject()));
    }

    return identity;
}

KickSender KickSender::fromJson(const QJsonObject &json)
{
    KickSender sender;
    // Try both "user_id" (official API) and "id" (Pusher format) for user ID
    if (json.contains("user_id"))
    {
        sender.id = json["user_id"].toInt();
    }
    else
    {
        sender.id = json["id"].toInt();
    }
    sender.username = json["username"].toString();
    sender.slug = json["slug"].toString();
    // Handle both "identity" and "channel_slug" for slug fallback
    if (sender.slug.isEmpty() && json.contains("channel_slug"))
    {
        sender.slug = json["channel_slug"].toString();
    }
    sender.identity = KickIdentity::fromJson(json["identity"].toObject());
    return sender;
}

KickEmote KickEmote::fromJson(const QJsonObject &json)
{
    KickEmote emote;

    // Handle both "emote_id" and "id" field names
    if (json.contains("emote_id"))
    {
        emote.emoteId = json["emote_id"].toString();
    }
    else if (json.contains("id"))
    {
        // Could be string or int
        if (json["id"].isString())
        {
            emote.emoteId = json["id"].toString();
        }
        else
        {
            emote.emoteId = QString::number(json["id"].toInt());
        }
    }

    // Store emote name if provided
    if (json.contains("name"))
    {
        emote.emoteName = json["name"].toString();
    }

    // Handle both position formats:
    // Format 1: positions array: [{ "s": start, "e": end }, ...]
    // Format 2: direct start/end fields on the emote object
    if (json.contains("positions"))
    {
        const auto positionsArray = json["positions"].toArray();
        emote.positions.reserve(positionsArray.size());
        for (const auto &posVal : positionsArray)
        {
            QJsonObject posObj = posVal.toObject();
            KickEmotePosition pos;
            pos.start = posObj["s"].toInt();
            pos.end = posObj["e"].toInt();
            emote.positions.push_back(pos);
        }
    }
    else if (json.contains("start") && json.contains("end"))
    {
        // Direct start/end format
        KickEmotePosition pos;
        pos.start = json["start"].toInt();
        pos.end = json["end"].toInt();
        emote.positions.push_back(pos);
    }

    return emote;
}

KickMessage KickMessage::fromJson(const QJsonObject &json)
{
    KickMessage msg;
    msg.id = json["id"].toString();
    msg.chatroomId = json["chatroom_id"].toInt();
    msg.content = json["content"].toString();
    msg.type = json["type"].toString();
    msg.createdAt =
        QDateTime::fromString(json["created_at"].toString(), Qt::ISODateWithMs);
    msg.sender = KickSender::fromJson(json["sender"].toObject());

    // Parse emotes array
    const auto emotesArray = json["emotes"].toArray();
    msg.emotes.reserve(emotesArray.size());
    for (const auto &emoteVal : emotesArray)
    {
        msg.emotes.push_back(KickEmote::fromJson(emoteVal.toObject()));
    }

    return msg;
}

bool KickMessage::isChatMessage() const
{
    return this->type == "message";
}

}  // namespace chatterino
