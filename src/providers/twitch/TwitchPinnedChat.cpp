#include "providers/twitch/TwitchPinnedChat.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace {

using namespace chatterino;

constexpr int TIMEOUT_MS = 15000;

// Twitch's own web client id. The query is anonymous, no token is attached.
constexpr const char *WEB_CLIENT_ID = "kimne78kx3ncx6brgo4mv6wki5h1ko";
constexpr const char *GET_PINNED_CHAT_HASH =
    "2d099d4c9b6af80a07d8440140c4f3dbb04d516b35c401aab7ce8f60765308d5";

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

std::optional<TwitchPinnedMessage> parseTwitchPinnedChat(
    const QJsonValue &response)
{
    // gql answers a batched request with an array, a single one with an object
    auto root = response.isArray() ? response.toArray().first() : response;

    auto edges = root.toObject()["data"]
                     .toObject()["channel"]
                     .toObject()["pinnedChatMessages"]
                     .toObject()["edges"]
                     .toArray();
    if (edges.isEmpty())
    {
        return std::nullopt;
    }

    auto node = edges.first().toObject()["node"].toObject();
    if (node.isEmpty())
    {
        return std::nullopt;
    }

    TwitchPinnedMessage pin;
    pin.pinId = node["id"].toString();

    auto message = node["pinnedMessage"].toObject();
    pin.messageId = message["id"].toString();
    pin.text = message["content"].toObject()["text"].toString();

    auto sender = message["sender"].toObject();
    pin.authorName = sender["displayName"].toString();
    pin.authorLogin = sender["login"].toString();
    if (pin.authorLogin.isEmpty())
    {
        pin.authorLogin = pin.authorName;
    }
    pin.authorColor = sender["chatColor"].toString();

    auto pinnedBy = node["pinnedBy"].toObject();
    pin.pinnerName = pinnedBy["displayName"].toString();
    if (pin.pinnerName.isEmpty())
    {
        pin.pinnerName = pinnedBy["login"].toString();
    }

    pin.endsAt = parseTimestamp(node["endsAt"]);
    pin.pinnedAt = parseTimestamp(node["updatedAt"]);
    if (!pin.pinnedAt.isValid())
    {
        pin.pinnedAt = QDateTime::currentDateTimeUtc();
    }

    if (pin.text.isEmpty())
    {
        return std::nullopt;
    }

    return pin;
}

void fetchTwitchPinnedChat(
    const QString &channelId,
    std::function<void(std::optional<TwitchPinnedMessage>)> onSuccess,
    std::function<void(const QString &)> onError)
{
    if (channelId.isEmpty())
    {
        onError("Missing channel id");
        return;
    }

    QJsonObject query{
        {"operationName", "GetPinnedChat"},
        {"variables", QJsonObject{{"channelID", channelId}, {"count", 1}}},
        {"extensions",
         QJsonObject{{"persistedQuery",
                      QJsonObject{{"version", 1},
                                  {"sha256Hash", GET_PINNED_CHAT_HASH}}}}},
    };

    NetworkRequest("https://gql.twitch.tv/gql", NetworkRequestType::Post)
        .timeout(TIMEOUT_MS)
        .header("Client-Id", WEB_CLIENT_ID)
        .header("Content-Type", "application/json")
        .payload(
            QJsonDocument(QJsonArray{query}).toJson(QJsonDocument::Compact))
        .onSuccess([onSuccess = std::move(onSuccess)](
                       const NetworkResult &result) mutable {
            onSuccess(parseTwitchPinnedChat(result.parseJsonValue()));
        })
        .onError([onError = std::move(onError)](const NetworkResult &result) {
            onError(result.formatError());
        })
        .execute();
}

}  // namespace chatterino
