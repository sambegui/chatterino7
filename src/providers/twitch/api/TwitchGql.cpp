#include "providers/twitch/api/TwitchGql.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include <utility>

namespace {

using namespace chatterino;

constexpr int TIMEOUT_MS = 20000;
constexpr const char *ENDPOINT = "https://gql.twitch.tv/gql";
constexpr const char *WEB_CLIENT_ID = "kimne78kx3ncx6brgo4mv6wki5h1ko";
constexpr const char *TV_CLIENT_ID = "ue6666qo983tsx6so1t0vnawi233wa";
constexpr const char *TV_ORIGIN = "https://android.tv.twitch.tv";
constexpr const char *TV_USER_AGENT =
    "Mozilla/5.0 (Linux; Android 7.1; Smart Box C1) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36";

/// Twitch expects a stable per-install id on these requests.
QString deviceId()
{
    static QString id =
        QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').left(32);
    return id;
}

NetworkRequest makeRequest(const QJsonObject &payload, gql::Client client)
{
    auto request =
        NetworkRequest(ENDPOINT, NetworkRequestType::Post)
            .timeout(TIMEOUT_MS)
            .header("Content-Type", "application/json")
            .header("X-Device-Id", deviceId().toUtf8())
            .header("Authorization", ("OAuth " + gql::token()).toUtf8())
            .payload(QJsonDocument(QJsonArray{payload})
                         .toJson(QJsonDocument::Compact));

    if (client == gql::Client::AndroidTV)
    {
        return std::move(request)
            .header("Client-Id", TV_CLIENT_ID)
            .header("Origin", TV_ORIGIN)
            .header("Referer", QString(TV_ORIGIN) + "/")
            .header("User-Agent", TV_USER_AGENT);
    }

    return std::move(request).header("Client-Id", WEB_CLIENT_ID);
}

/// gql answers a batched request with an array of results.
QJsonObject firstResult(const QJsonValue &response)
{
    if (response.isArray())
    {
        return response.toArray().first().toObject();
    }
    return response.toObject();
}

void run(const QJsonObject &payload, gql::SuccessCallback onSuccess,
         gql::FailureCallback onFailure, gql::Client client)
{
    auto reason = gql::unavailableReason();
    if (!reason.isEmpty())
    {
        onFailure(reason);
        return;
    }

    makeRequest(payload, client)
        .onSuccess([onSuccess = std::move(onSuccess),
                    onFailure](const NetworkResult &result) mutable {
            auto root = firstResult(result.parseJsonValue());
            if (root.isEmpty())
            {
                onFailure("Could not read Twitch's response");
                return;
            }

            auto error = gql::firstError(root);
            if (!error.isEmpty())
            {
                onFailure(error);
                return;
            }

            onSuccess(root.value("data").toObject());
        })
        .onError(
            [onFailure = std::move(onFailure)](const NetworkResult &result) {
                // 401 here means the pasted token is stale, which is the common case
                if (result.status() == 401)
                {
                    onFailure(
                        "Twitch rejected the saved token. Paste a fresh one in "
                        "Settings.");
                    return;
                }
                onFailure(result.formatError());
            })
        .execute();
}

}  // namespace

namespace chatterino::gql {

QString token()
{
    auto raw = getSettings()->twitchGqlToken.getValue().trimmed();

    if (raw.startsWith("Authorization:", Qt::CaseInsensitive))
    {
        raw = raw.mid(QStringLiteral("Authorization:").size()).trimmed();
    }
    if (raw.startsWith("OAuth ", Qt::CaseInsensitive))
    {
        raw = raw.mid(QStringLiteral("OAuth ").size()).trimmed();
    }

    return raw;
}

bool isEnabled()
{
    return getSettings()->enableTwitchGql && !token().isEmpty();
}

QString unavailableReason()
{
    if (!getSettings()->enableTwitchGql)
    {
        return "This needs Twitch's private API, which is off. Turn it on in "
               "Settings if you accept the risk.";
    }
    if (token().isEmpty())
    {
        return "No Twitch token saved. Paste one in Settings to use this.";
    }
    return {};
}

QString firstError(const QJsonObject &response)
{
    auto errors = response.value("errors").toArray();
    if (errors.isEmpty())
    {
        return {};
    }

    auto message = errors.first().toObject().value("message").toString();
    return message.isEmpty() ? QStringLiteral("Twitch rejected the request")
                             : message;
}

void persistedQuery(const QString &operationName, const QString &sha256Hash,
                    const QJsonObject &variables, SuccessCallback onSuccess,
                    FailureCallback onFailure, Client client)
{
    run(
        QJsonObject{
            {"operationName", operationName},
            {"variables", variables},
            {"extensions",
             QJsonObject{
                 {"persistedQuery",
                  QJsonObject{{"version", 1}, {"sha256Hash", sha256Hash}}}}},
        },
        std::move(onSuccess), std::move(onFailure), client);
}

void query(const QString &operationName, const QString &document,
           const QJsonObject &variables, SuccessCallback onSuccess,
           FailureCallback onFailure, Client client)
{
    run(
        QJsonObject{
            {"operationName", operationName},
            {"query", document},
            {"variables", variables},
        },
        std::move(onSuccess), std::move(onFailure), client);
}

void voteInPoll(const QString &pollId, const QString &choiceId,
                const QString &userId, std::function<void()> onSuccess,
                FailureCallback onFailure)
{
    static const char *document = R"(
        mutation VoteInPoll($input: VoteInPollInput!) {
            voteInPoll(input: $input) {
                error {
                    code
                }
            }
        }
    )";

    QJsonObject input{
        {"pollID", pollId},
        {"choiceID", choiceId},
        {"userID", userId},
        {"voteID", QUuid::createUuid().toString(QUuid::WithoutBraces)},
    };

    query(
        "VoteInPoll", document, {{"input", input}},
        [onSuccess = std::move(onSuccess), onFailure](const QJsonObject &data) {
            // the mutation reports refusals in its own payload, not in errors
            auto code = data.value("voteInPoll")
                            .toObject()
                            .value("error")
                            .toObject()
                            .value("code")
                            .toString();
            if (!code.isEmpty())
            {
                onFailure(code);
                return;
            }
            onSuccess();
        },
        std::move(onFailure));
}

void makePrediction(const QString &eventId, const QString &outcomeId,
                    int points, std::function<void()> onSuccess,
                    FailureCallback onFailure)
{
    QJsonObject input{
        {"eventID", eventId},
        {"outcomeID", outcomeId},
        {"points", points},
        {"transactionID",
         QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')},
    };

    persistedQuery(
        "MakePrediction",
        "b44682ecc88358817009f20e69d75081b1e58825bb40aa53d5dbadcc17c881d8",
        {{"input", input}},
        [onSuccess = std::move(onSuccess), onFailure](const QJsonObject &data) {
            auto error = data.value("makePrediction")
                             .toObject()
                             .value("error")
                             .toObject()
                             .value("code")
                             .toString();
            if (!error.isEmpty())
            {
                onFailure(error);
                return;
            }
            onSuccess();
        },
        std::move(onFailure));
}

void channelPointsContext(
    const QString &channelLogin,
    std::function<void(qint64 balance, std::vector<ChannelPointReward>)>
        onSuccess,
    FailureCallback onFailure)
{
    QJsonObject variables{
        {"channelLogin", channelLogin},
        {"includeGoalTypes", QJsonArray{"CREATOR", "BOOST"}},
    };

    persistedQuery(
        "ChannelPointsContext",
        "7fe050e3761eb2cf258d70ee1a21cbd76fa8cf3d7e7b12fc437e7029d446b5e3",
        variables,
        [onSuccess = std::move(onSuccess)](const QJsonObject &data) {
            auto channel = data.value("community").toObject().isEmpty()
                               ? data.value("channel").toObject()
                               : data.value("community")
                                     .toObject()
                                     .value("channel")
                                     .toObject();

            auto balance = static_cast<qint64>(channel.value("self")
                                                   .toObject()
                                                   .value("communityPoints")
                                                   .toObject()
                                                   .value("balance")
                                                   .toDouble());

            std::vector<ChannelPointReward> rewards;
            auto settings = channel.value("communityPointsSettings").toObject();
            for (const auto &value : settings.value("customRewards").toArray())
            {
                auto object = value.toObject();
                if (!object.value("isEnabled").toBool(true) ||
                    object.value("isPaused").toBool(false))
                {
                    continue;
                }

                ChannelPointReward reward;
                reward.id = object.value("id").toString();
                reward.title = object.value("title").toString();
                reward.prompt = object.value("prompt").toString();
                reward.cost = object.value("cost").toInt();
                reward.needsInput =
                    object.value("isUserInputRequired").toBool();

                if (!reward.id.isEmpty())
                {
                    rewards.push_back(std::move(reward));
                }
            }

            onSuccess(balance, std::move(rewards));
        },
        std::move(onFailure), Client::AndroidTV);
}

void redeemChannelPointReward(const QString &channelId,
                              const ChannelPointReward &reward,
                              const QString &textInput,
                              std::function<void()> onSuccess,
                              FailureCallback onFailure)
{
    QJsonObject input{
        {"channelID", channelId},
        {"cost", reward.cost},
        {"pricingType", "POINTS"},
        {"rewardID", reward.id},
        {"title", reward.title},
        {"transactionID",
         QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')},
        {"prompt", reward.prompt.trimmed().isEmpty()
                       ? QJsonValue(QJsonValue::Null)
                       : QJsonValue(reward.prompt)},
    };
    if (!textInput.trimmed().isEmpty())
    {
        input["textInput"] = textInput;
    }

    persistedQuery(
        "RedeemCustomReward",
        "d56249a7adb4978898ea3412e196688d4ac3cea1c0c2dfd65561d229ea5dcc42",
        {{"input", input}},
        [onSuccess = std::move(onSuccess), onFailure](const QJsonObject &data) {
            auto error = data.value("redeemCommunityPointsCustomReward")
                             .toObject()
                             .value("error")
                             .toObject()
                             .value("code")
                             .toString();
            if (!error.isEmpty())
            {
                onFailure(error);
                return;
            }
            onSuccess();
        },
        std::move(onFailure), Client::AndroidTV);
}

void usercardMessages(const QString &channelId, const QString &senderId,
                      const QString &cursor,
                      std::function<void(UsercardMessagePage)> onSuccess,
                      FailureCallback onFailure)
{
    QJsonObject variables{
        {"channelID", channelId},
        {"senderID", senderId},
    };
    if (!cursor.isEmpty())
    {
        variables["cursor"] = cursor;
    }

    persistedQuery(
        "ViewerCardModLogsMessagesBySender",
        "eb4e9869e1bb0b3ed553e1ed657fa09f8553781093569c3a5813ad09ee9c0776",
        variables,
        [onSuccess = std::move(onSuccess)](const QJsonObject &data) {
            auto messages = data.value("viewerCardModLogs")
                                .toObject()
                                .value("messages")
                                .toObject();

            UsercardMessagePage page;
            page.hasNextPage = messages.value("pageInfo")
                                   .toObject()
                                   .value("hasNextPage")
                                   .toBool();

            for (const auto &value : messages.value("edges").toArray())
            {
                auto edge = value.toObject();
                auto node = edge.value("node").toObject();

                UsercardMessage message;
                message.id = node.value("id").toString();
                message.text =
                    node.value("content").toObject().value("text").toString();
                message.sentAt = node.value("sentAt").toString();
                message.isDeleted = node.value("isDeleted").toBool();

                if (!message.text.isEmpty())
                {
                    page.messages.push_back(std::move(message));
                }

                page.nextCursor = edge.value("cursor").toString();
            }

            onSuccess(std::move(page));
        },
        std::move(onFailure), Client::AndroidTV);
}

}  // namespace chatterino::gql
