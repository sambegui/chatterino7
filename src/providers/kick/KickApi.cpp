#include "providers/kick/KickApi.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickAccount.hpp"

#include <pajlada/signals/scoped-connection.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace chatterino {

QString translateKickError(KickError error, const QString &apiMessage,
                           int secondsUntilReset)
{
    switch (error)
    {
        case KickError::MissingText:
            return QStringLiteral("You can't send an empty message.");

        case KickError::BadRequest:
            if (!apiMessage.isEmpty())
            {
                return QStringLiteral("Failed to send message: %1")
                    .arg(apiMessage);
            }
            return QStringLiteral("Failed to send message: Invalid request.");

        case KickError::Forbidden:
            return QStringLiteral(
                "You don't have permission to send messages in this channel.");

        case KickError::RateLimited:
            if (secondsUntilReset > 0)
            {
                return QStringLiteral("You're sending messages too quickly. "
                                      "Kick allows 3 messages "
                                      "per second. Please wait %1 seconds "
                                      "before sending another "
                                      "message.")
                    .arg(secondsUntilReset);
            }
            return QStringLiteral(
                "You're sending messages too quickly. Kick allows 3 messages "
                "per "
                "second. Please wait before sending another message.");

        case KickError::UserMissingScope:
            return QStringLiteral("Missing required scope. Re-login with your "
                                  "account and try again.");

        case KickError::TokenExpired:
            return QStringLiteral(
                "Your session has expired. Please log in again.");

        case KickError::ConnectionFailed:
            return QStringLiteral("Failed to connect to Kick chat. Check your "
                                  "internet connection "
                                  "or verify the channel name is correct.");

        case KickError::ChannelNotFound:
            return QStringLiteral("Channel not found. Please verify the "
                                  "channel name is correct.");

        case KickError::MessageTooLong:
            return QStringLiteral(
                "Your message is too long. Kick messages can be at most 500 "
                "characters.");

        case KickError::Unknown:
        default:
            if (!apiMessage.isEmpty())
            {
                return QStringLiteral("An error occurred: %1").arg(apiMessage);
            }
            return QStringLiteral("An unknown error occurred.");
    }
}

KickError mapHttpStatusToKickError(int httpStatus, const QString &apiMessage)
{
    switch (httpStatus)
    {
        case 400:
            // Check if it's a message length issue
            if (apiMessage.contains(QStringLiteral("too long"),
                                    Qt::CaseInsensitive) ||
                apiMessage.contains(QStringLiteral("500"), Qt::CaseInsensitive))
            {
                return KickError::MessageTooLong;
            }
            return KickError::BadRequest;

        case 401:
            // Distinguish between missing scope and expired token
            if (apiMessage.contains(QStringLiteral("expired"),
                                    Qt::CaseInsensitive))
            {
                return KickError::TokenExpired;
            }
            return KickError::UserMissingScope;

        case 403:
            return KickError::Forbidden;

        case 404:
            return KickError::ChannelNotFound;

        case 429:
            return KickError::RateLimited;

        default:
            return KickError::Unknown;
    }
}

KickApi::KickApi(QObject *parent)
    : QObject(parent)
    , networkManager_(std::make_unique<QNetworkAccessManager>(this))
{
}

KickApi::~KickApi() = default;

void KickApi::setAccount(std::shared_ptr<KickAccount> account)
{
    this->account_ = std::move(account);
}

std::shared_ptr<KickAccount> KickApi::getAccount() const
{
    return this->account_;
}

void KickApi::resolveChannelInfo(const QString &channelSlug,
                                 std::function<void(ChannelInfo info)> callback)
{
    // Use the channel API to get broadcaster info
    // Note: This uses unofficial endpoint as official API doesn't expose this
    QString url = QString("%1/channels/%2")
                      .arg(QString::fromLatin1(KICK_CHANNEL_API), channelSlug);

    qCDebug(chatterinoKick)
        << "Resolving channel info for:" << channelSlug << "URL:" << url;

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setRawHeader("Accept", "application/json");

    // the response only carries a "role" for the caller when it knows who is
    // asking, and that role is what tells us whether we can moderate here
    if (this->account_ && this->account_->isAuthenticated())
    {
        request.setRawHeader("Authorization",
                             QString("Bearer %1")
                                 .arg(this->account_->getAccessToken())
                                 .toUtf8());
    }

    QNetworkReply *reply = this->networkManager_->get(request);

    QObject::connect(
        reply, &QNetworkReply::finished, this, [reply, callback, channelSlug] {
            reply->deleteLater();

            ChannelInfo info;
            info.slug = channelSlug;

            if (reply->error() != QNetworkReply::NoError)
            {
                qCWarning(chatterinoKick)
                    << "Failed to resolve channel info for" << channelSlug
                    << ":" << reply->errorString();
                callback(info);
                return;
            }

            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (!doc.isObject())
            {
                qCWarning(chatterinoKick) << "Invalid channel response";
                callback(info);
                return;
            }

            QJsonObject obj = doc.object();

            // Extract broadcaster user_id from response
            // The structure includes: { "user_id": 12345, "chatroom": { "id": 67890 } }
            if (obj.contains("user_id"))
            {
                info.broadcasterUserId = obj["user_id"].toInt();
            }
            else if (obj.contains("user") && obj["user"].isObject())
            {
                // Alternative structure: { "user": { "id": 12345 } }
                info.broadcasterUserId = obj["user"].toObject()["id"].toInt();
            }

            // Extract chatroom ID and the chat restrictions alongside it
            if (obj.contains("chatroom") && obj["chatroom"].isObject())
            {
                QJsonObject chatroom = obj["chatroom"].toObject();
                info.chatroomId = chatroom["id"].toInt();

                info.roomModes.subscribersOnly =
                    chatroom["subscribers_mode"].toBool();
                info.roomModes.emotesOnly = chatroom["emotes_mode"].toBool();
                if (chatroom["slow_mode"].toBool())
                {
                    info.roomModes.slowModeInterval =
                        chatroom["message_interval"].toInt();
                }
                if (chatroom["followers_mode"].toBool())
                {
                    info.roomModes.followersOnlyDuration =
                        chatroom["following_min_duration"].toInt();
                }
            }
            else if (obj.contains("id"))
            {
                // Sometimes the channel ID is used as chatroom ID
                info.chatroomId = obj["id"].toInt();
            }

            // Extract display name
            if (obj.contains("user") && obj["user"].isObject())
            {
                info.displayName =
                    obj["user"].toObject()["username"].toString();
            }

            // Present only when the request was authenticated
            info.userRole = obj["role"].toString();

            // Extract livestream info (check if channel is live)
            if (obj.contains("livestream") && !obj["livestream"].isNull())
            {
                QJsonObject livestream = obj["livestream"].toObject();
                info.isLive = livestream["is_live"].toBool();
                info.streamTitle = livestream["session_title"].toString();
                info.viewerCount = livestream["viewer_count"].toInt();
            }

            if (info.broadcasterUserId > 0 && info.chatroomId > 0)
            {
                info.success = true;
                qCDebug(chatterinoKick)
                    << "Resolved channel" << channelSlug
                    << "- broadcaster ID:" << info.broadcasterUserId
                    << "- isLive:" << info.isLive
                    << "- chatroom ID:" << info.chatroomId;
            }
            else
            {
                qCWarning(chatterinoKick)
                    << "Incomplete channel info for" << channelSlug
                    << "- broadcaster ID:" << info.broadcasterUserId
                    << "- chatroom ID:" << info.chatroomId;
            }

            callback(info);
        });
}

void KickApi::resolveBroadcasterId(
    const QString &channelSlug,
    std::function<void(int broadcasterUserId, bool success)> callback)
{
    // Delegate to resolveChannelInfo for backward compatibility
    this->resolveChannelInfo(channelSlug, [callback](ChannelInfo info) {
        callback(info.broadcasterUserId, info.success);
    });
}

void KickApi::sendMessage(int broadcasterUserId, const QString &message,
                          std::function<void(KickApiResult result)> callback)
{
    if (!this->account_ || !this->account_->isAuthenticated())
    {
        KickApiResult result;
        result.success = false;
        result.errorMessage = translateKickError(KickError::UserMissingScope);
        callback(result);
        return;
    }

    // Check rate limit
    if (this->isRateLimited())
    {
        KickApiResult result;
        result.success = false;
        result.rateLimit = this->rateLimitInfo_;

        int secondsUntilReset =
            QDateTime::currentDateTime().secsTo(this->rateLimitInfo_.resetAt);
        if (secondsUntilReset < 0)
        {
            secondsUntilReset = 0;
        }

        result.errorMessage =
            translateKickError(KickError::RateLimited, {}, secondsUntilReset);
        this->rateLimited.invoke(secondsUntilReset);

        callback(result);
        return;
    }

    // Check if token needs refresh
    if (this->account_->isTokenExpired())
    {
        this->refreshAndRetry([this, broadcasterUserId, message, callback] {
            this->sendMessage(broadcasterUserId, message, callback);
        });
        return;
    }

    // Official Kick API endpoint (from Context7: docs.kick.com/apis/chat)
    // POST https://api.kick.com/public/v1/chat
    // Body: { "content": string, "type": "user"|"bot", "broadcaster_user_id": int }
    QString url = QString("%1/chat").arg(QString::fromLatin1(KICK_API_BASE));

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader(
        "Authorization",
        QString("Bearer %1").arg(this->account_->getAccessToken()).toUtf8());

    // Request body per official API spec (docs.kick.com)
    // Required fields: content, type
    // broadcaster_user_id is required when type="user"
    QJsonObject body;
    body["content"] = message;       // Message text (max 500 chars)
    body["type"] = QString("user");  // Send as user (not bot)
    body["broadcaster_user_id"] =
        broadcasterUserId;  // Required for type="user"
    QJsonDocument bodyDoc(body);

    QNetworkReply *reply = this->networkManager_->post(
        request, bodyDoc.toJson(QJsonDocument::Compact));

    QObject::connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, callback, broadcasterUserId, message] {
            reply->deleteLater();

            // Update rate limit info from headers
            this->updateRateLimitFromReply(reply);

            KickApiResult result;
            result.rateLimit = this->rateLimitInfo_;

            int statusCode =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            result.httpStatus = statusCode;

            if (reply->error() != QNetworkReply::NoError)
            {
                result = this->handleErrorResponse(reply);

                // Handle specific error codes
                if (statusCode == 401)
                {
                    // Token expired, try refresh
                    this->refreshAndRetry([this, broadcasterUserId, message,
                                           callback] {
                        this->sendMessage(broadcasterUserId, message, callback);
                    });
                    return;
                }
                else if (statusCode == 429)
                {
                    // Rate limited
                    int secondsUntilReset = QDateTime::currentDateTime().secsTo(
                        this->rateLimitInfo_.resetAt);
                    this->rateLimited.invoke(secondsUntilReset);
                }
                else if (statusCode == 403)
                {
                    // Banned or no permission - already translated by handleErrorResponse
                }

                callback(result);
                return;
            }

            result.success = true;
            qCDebug(chatterinoKick)
                << "Message sent successfully to broadcaster"
                << broadcasterUserId;
            callback(result);
        });
}

void KickApi::sendAuthorizedRequest(
    const QByteArray &verb, const QString &endpoint,
    const std::optional<QJsonObject> &body,
    std::function<void(KickApiResult result)> callback,
    std::function<void()> retry)
{
    if (!this->account_ || !this->account_->isAuthenticated())
    {
        KickApiResult result;
        result.errorMessage = translateKickError(KickError::UserMissingScope);
        callback(result);
        return;
    }

    if (this->isRateLimited())
    {
        KickApiResult result;
        result.rateLimit = this->rateLimitInfo_;

        auto secondsUntilReset = static_cast<int>(std::max<qint64>(
            0,
            QDateTime::currentDateTime().secsTo(this->rateLimitInfo_.resetAt)));
        result.errorMessage =
            translateKickError(KickError::RateLimited, {}, secondsUntilReset);
        this->rateLimited.invoke(secondsUntilReset);

        callback(result);
        return;
    }

    if (this->account_->isTokenExpired())
    {
        this->refreshAndRetry(std::move(retry));
        return;
    }

    QNetworkRequest request{QUrl{
        QString("%1/%2").arg(QString::fromLatin1(KICK_API_BASE), endpoint)}};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader(
        "Authorization",
        QString("Bearer %1").arg(this->account_->getAccessToken()).toUtf8());

    QByteArray payload;
    if (body)
    {
        payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
    }

    QNetworkReply *reply =
        this->networkManager_->sendCustomRequest(request, verb, payload);

    QObject::connect(
        reply, &QNetworkReply::finished, this, [this, reply, callback, retry] {
            reply->deleteLater();
            this->updateRateLimitFromReply(reply);

            KickApiResult result;
            result.rateLimit = this->rateLimitInfo_;
            result.httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();

            if (reply->error() != QNetworkReply::NoError)
            {
                result = this->handleErrorResponse(reply);

                if (result.httpStatus == 401)
                {
                    this->refreshAndRetry(retry);
                    return;
                }
                if (result.httpStatus == 429)
                {
                    this->rateLimited.invoke(static_cast<int>(std::max<qint64>(
                        0, QDateTime::currentDateTime().secsTo(
                               this->rateLimitInfo_.resetAt))));
                }

                callback(result);
                return;
            }

            result.success = true;
            callback(result);
        });
}

void KickApi::banUser(int broadcasterUserId, int userId,
                      std::optional<int> durationMinutes, const QString &reason,
                      std::function<void(KickApiResult result)> callback)
{
    QJsonObject body{
        {"broadcaster_user_id", broadcasterUserId},
        {"user_id", userId},
    };
    if (durationMinutes)
    {
        body["duration"] = *durationMinutes;
    }
    if (!reason.isEmpty())
    {
        body["reason"] = reason;
    }

    this->sendAuthorizedRequest(
        "POST", "moderation/bans", body, callback,
        [this, broadcasterUserId, userId, durationMinutes, reason, callback] {
            this->banUser(broadcasterUserId, userId, durationMinutes, reason,
                          callback);
        });
}

void KickApi::unbanUser(int broadcasterUserId, int userId,
                        std::function<void(KickApiResult result)> callback)
{
    QJsonObject body{
        {"broadcaster_user_id", broadcasterUserId},
        {"user_id", userId},
    };

    this->sendAuthorizedRequest("DELETE", "moderation/bans", body, callback,
                                [this, broadcasterUserId, userId, callback] {
                                    this->unbanUser(broadcasterUserId, userId,
                                                    callback);
                                });
}

void KickApi::deleteChatMessage(
    const QString &messageId,
    std::function<void(KickApiResult result)> callback)
{
    auto endpoint = QString("chat/%1").arg(
        QString::fromUtf8(QUrl::toPercentEncoding(messageId)));

    this->sendAuthorizedRequest("DELETE", endpoint, std::nullopt, callback,
                                [this, messageId, callback] {
                                    this->deleteChatMessage(messageId,
                                                            callback);
                                });
}

KickRateLimitInfo KickApi::getRateLimitInfo() const
{
    return this->rateLimitInfo_;
}

bool KickApi::isRateLimited() const
{
    return this->rateLimitInfo_.remaining <= 0 &&
           QDateTime::currentDateTime() < this->rateLimitInfo_.resetAt;
}

void KickApi::updateRateLimitFromReply(QNetworkReply *reply)
{
    // Parse X-RateLimit-* headers
    QByteArray limitHeader = reply->rawHeader("X-RateLimit-Limit");
    QByteArray remainingHeader = reply->rawHeader("X-RateLimit-Remaining");
    QByteArray resetHeader = reply->rawHeader("X-RateLimit-Reset");

    if (!limitHeader.isEmpty())
    {
        this->rateLimitInfo_.limit = limitHeader.toInt();
    }
    if (!remainingHeader.isEmpty())
    {
        this->rateLimitInfo_.remaining = remainingHeader.toInt();
    }
    if (!resetHeader.isEmpty())
    {
        // Reset header is typically a Unix timestamp
        qint64 resetTimestamp = resetHeader.toLongLong();
        this->rateLimitInfo_.resetAt =
            QDateTime::fromSecsSinceEpoch(resetTimestamp);
    }
}

KickApiResult KickApi::handleErrorResponse(QNetworkReply *reply)
{
    KickApiResult result;
    result.success = false;
    result.httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    QString rawApiMessage;
    if (doc.isObject())
    {
        QJsonObject obj = doc.object();
        if (obj.contains("message"))
        {
            rawApiMessage = obj["message"].toString();
        }
        else if (obj.contains("error"))
        {
            rawApiMessage = obj["error"].toString();
        }
        else
        {
            rawApiMessage = reply->errorString();
        }
    }
    else
    {
        rawApiMessage = reply->errorString();
    }

    // Map HTTP status to KickError and translate to user-friendly message
    KickError error =
        mapHttpStatusToKickError(result.httpStatus, rawApiMessage);

    // Calculate seconds until rate limit reset if applicable
    int secondsUntilReset = 0;
    if (error == KickError::RateLimited &&
        this->rateLimitInfo_.resetAt.isValid())
    {
        secondsUntilReset =
            QDateTime::currentDateTime().secsTo(this->rateLimitInfo_.resetAt);
        if (secondsUntilReset < 0)
        {
            secondsUntilReset = 0;
        }
    }

    // Translate to user-friendly message
    result.errorMessage =
        translateKickError(error, rawApiMessage, secondsUntilReset);

    qCWarning(chatterinoKick)
        << "API error:" << result.httpStatus << rawApiMessage
        << "-> User message:" << result.errorMessage;

    return result;
}

void KickApi::refreshAndRetry(std::function<void()> retryFn)
{
    if (!this->account_)
    {
        return;
    }

    qCDebug(chatterinoKick) << "Token expired, attempting refresh...";

    // Connect to token refresh result
    // Use shared_ptr to ensure the connection outlives the callback scope
    auto connection = std::make_shared<pajlada::Signals::ScopedConnection>();
    *connection = this->account_->tokenRefreshed.connect([retryFn, connection](
                                                             bool success) {
        if (success)
        {
            qCDebug(chatterinoKick) << "Token refreshed, retrying request...";
            retryFn();
        }
        else
        {
            qCWarning(chatterinoKick) << "Token refresh failed";
        }
        // Disconnect after handling by assigning an empty connection
        *connection = pajlada::Signals::ScopedConnection{};
    });

    this->account_->refreshAccessToken();
}

void KickApi::fetchChannelEmotes(
    const QString &channelSlug,
    std::function<void(ChannelEmotesResult result)> callback)
{
    // Public endpoint: https://kick.com/emotes/{channel}
    // Returns: [{ "emotes": [{ "id": int, "name": string, "subscribers_only": bool }] }]
    QString url = QString("https://kick.com/emotes/%1").arg(channelSlug);

    qCDebug(chatterinoKick) << "Fetching channel emotes for:" << channelSlug;

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                      "AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = this->networkManager_->get(request);

    QObject::connect(
        reply, &QNetworkReply::finished, this, [reply, callback, channelSlug] {
            reply->deleteLater();

            ChannelEmotesResult result;
            result.channelSlug = channelSlug;

            if (reply->error() != QNetworkReply::NoError)
            {
                result.errorMessage = reply->errorString();
                qCWarning(chatterinoKick)
                    << "Failed to fetch emotes for" << channelSlug << ":"
                    << result.errorMessage;
                callback(result);
                return;
            }

            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (!doc.isArray() || doc.array().isEmpty())
            {
                result.errorMessage = "Invalid emotes response";
                qCWarning(chatterinoKick)
                    << "Invalid emotes response for" << channelSlug;
                callback(result);
                return;
            }

            // Response is: [{ ..., "emotes": [...] }]
            QJsonObject channelObj = doc.array().first().toObject();
            QJsonArray emotesArray = channelObj["emotes"].toArray();

            result.emotes.reserve(emotesArray.size());
            for (const auto &emoteVal : emotesArray)
            {
                QJsonObject emoteObj = emoteVal.toObject();
                KickEmoteInfo emote;
                emote.id = emoteObj["id"].toInt();
                emote.name = emoteObj["name"].toString();
                emote.subscribersOnly = emoteObj["subscribers_only"].toBool();
                result.emotes.push_back(emote);
            }

            result.success = true;
            qCDebug(chatterinoKick) << "Fetched" << result.emotes.size()
                                    << "emotes for channel" << channelSlug;

            callback(result);
        });
}

void KickApi::fetchUserRoleInChannel(
    const QString &channelSlug,
    std::function<void(UserRoleResult result)> callback)
{
    // Public endpoint: https://kick.com/api/v2/channels/{channel}
    // Returns: { ..., "role": null | "subscriber" | "moderator" | "broadcaster" }
    QString url = QString("%1/channels/%2")
                      .arg(QString::fromLatin1(KICK_CHANNEL_API), channelSlug);

    qCDebug(chatterinoKick) << "Fetching user role in channel:" << channelSlug;

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                      "AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = this->networkManager_->get(request);

    QObject::connect(
        reply, &QNetworkReply::finished, this, [reply, callback, channelSlug] {
            reply->deleteLater();

            UserRoleResult result;
            result.channelSlug = channelSlug;

            if (reply->error() != QNetworkReply::NoError)
            {
                result.errorMessage = reply->errorString();
                qCWarning(chatterinoKick)
                    << "Failed to fetch role in" << channelSlug << ":"
                    << result.errorMessage;
                callback(result);
                return;
            }

            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (!doc.isObject())
            {
                result.errorMessage = "Invalid channel response";
                qCWarning(chatterinoKick)
                    << "Invalid channel response for" << channelSlug;
                callback(result);
                return;
            }

            QJsonObject obj = doc.object();

            // Extract role field
            // null = no special role (not subscribed)
            // "subscriber", "moderator", "broadcaster" = has access
            if (obj["role"].isNull())
            {
                result.role = QString();
                result.isSubscribed = false;
            }
            else
            {
                result.role = obj["role"].toString();
                // Any non-null role grants subscriber emote access
                result.isSubscribed = !result.role.isEmpty();
            }

            result.success = true;
            qCDebug(chatterinoKick)
                << "User role in" << channelSlug << ":"
                << (result.role.isEmpty() ? "none" : result.role)
                << "- isSubscribed:" << result.isSubscribed;

            callback(result);
        });
}

}  // namespace chatterino
