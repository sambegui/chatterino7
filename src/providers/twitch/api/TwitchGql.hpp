#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>
#include <vector>

namespace chatterino {

class NetworkResult;

/// Twitch's private GraphQL API.
///
/// Everything Twitch's own site can do that the public API cannot: pinning a
/// message, creating and resolving predictions, voting in polls, redeeming
/// channel points. Reaching it means presenting Twitch's web client id
/// alongside a user token, which their Developer Agreement does not permit, so
/// every call here is gated behind the enableTwitchGql setting and a token the
/// user supplies themselves.
///
/// Read-only queries that need no token do not belong here; see
/// TwitchPinnedChat for an anonymous one.
namespace gql {

/// True when the user has switched this on and given us a token.
bool isEnabled();

/// The user's token, stripped of any "OAuth "/"Authorization:" prefix.
QString token();

/// A short reason why a call cannot be made, or an empty string when it can.
QString unavailableReason();

using SuccessCallback = std::function<void(const QJsonObject &data)>;
using FailureCallback = std::function<void(const QString &error)>;

/// Which of Twitch's own clients to present as. Some operations are only
/// served to one of them: channel points want the TV client, the rest the web
/// one.
enum class Client {
    Web,
    AndroidTV,
};

/// Runs a persisted query by its operation name and hash.
///
/// @a onFailure receives Twitch's own error message where there is one, since
/// these operations fail for reasons worth showing (not a mod, message too
/// old, prediction already locked).
void persistedQuery(const QString &operationName, const QString &sha256Hash,
                    const QJsonObject &variables, SuccessCallback onSuccess,
                    FailureCallback onFailure, Client client = Client::Web);

/// Runs a GraphQL document directly, for operations with no stable hash.
void query(const QString &operationName, const QString &document,
           const QJsonObject &variables, SuccessCallback onSuccess,
           FailureCallback onFailure, Client client = Client::Web);

/// Pulls the first error message out of a GraphQL response, if any.
QString firstError(const QJsonObject &response);

/// Casts a free vote in a poll as @a userId.
void voteInPoll(const QString &pollId, const QString &choiceId,
                const QString &userId, std::function<void()> onSuccess,
                FailureCallback onFailure);

/// Puts @a points channel points on an outcome of a running prediction.
void makePrediction(const QString &eventId, const QString &outcomeId,
                    int points, std::function<void()> onSuccess,
                    FailureCallback onFailure);

struct ChannelPointReward {
    QString id;
    QString title;
    QString prompt;
    int cost = 0;
    /// True when redeeming it asks the viewer for text.
    bool needsInput = false;
};

/// Reads the viewer's point balance and the rewards they can redeem here.
void channelPointsContext(
    const QString &channelLogin,
    std::function<void(qint64 balance, std::vector<ChannelPointReward>)>
        onSuccess,
    FailureCallback onFailure);

/// Redeems a reward, optionally with the text it asked for.
void redeemChannelPointReward(const QString &channelId,
                              const ChannelPointReward &reward,
                              const QString &textInput,
                              std::function<void()> onSuccess,
                              FailureCallback onFailure);

struct UsercardMessage {
    QString id;
    QString text;
    /// ISO 8601, as Twitch sends it.
    QString sentAt;
    bool isDeleted = false;
};

struct UsercardMessagePage {
    std::vector<UsercardMessage> messages;
    QString nextCursor;
    bool hasNextPage = false;
};

/// Reads a page of one user's past messages in a channel, newest first.
///
/// This is the channel's moderator log, so it only answers for a channel the
/// user moderates. Pass the cursor from a previous page to continue.
void usercardMessages(const QString &channelId, const QString &senderId,
                      const QString &cursor,
                      std::function<void(UsercardMessagePage)> onSuccess,
                      FailureCallback onFailure);

}  // namespace gql

}  // namespace chatterino
