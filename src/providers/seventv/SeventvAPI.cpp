#include "providers/seventv/SeventvAPI.hpp"

#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace {

using namespace chatterino::literals;

const QString API_URL_USER_TWITCH = u"https://7tv.io/v3/users/twitch/%1"_s;
const QString API_URL_USER_KICK = u"https://7tv.io/v3/users/KICK/%1"_s;
const QString API_URL_EMOTE_SET = u"https://7tv.io/v3/emote-sets/%1"_s;
const QString API_URL_PRESENCES = u"https://7tv.io/v3/users/%1/presences"_s;

}  // namespace

// NOLINTBEGIN(readability-convert-member-functions-to-static)
namespace chatterino {

void SeventvAPI::getUserByTwitchID(
    const QString &twitchID, SuccessCallback<const QJsonObject &> &&onSuccess,
    ErrorCallback &&onError)
{
    NetworkRequest(API_URL_USER_TWITCH.arg(twitchID), NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::getUserByKickID(
    const QString &kickUserID, SuccessCallback<const QJsonObject &> &&onSuccess,
    ErrorCallback &&onError)
{
    // Check cache first (read lock)
    {
        std::shared_lock lock(this->kickUserCacheMutex_);
        auto it = this->kickUserCache_.find(kickUserID);
        if (it != this->kickUserCache_.end())
        {
            const auto &entry = it->second;
            if (!entry.pending)
            {
                // We have a cached result
                if (entry.data.has_value())
                {
                    // Found - call success callback
                    onSuccess(entry.data.value());
                }
                // else: 404/not found - silently return (don't call error)
                return;
            }
            // Request is pending, skip making a new one
            return;
        }
    }

    // Mark as pending (write lock)
    {
        std::unique_lock lock(this->kickUserCacheMutex_);
        // Double-check in case another thread already started the request
        auto it = this->kickUserCache_.find(kickUserID);
        if (it != this->kickUserCache_.end())
        {
            return;  // Already pending or cached
        }
        this->kickUserCache_[kickUserID] =
            KickUserCacheEntry{std::nullopt, true};
    }

    // 7TV supports Kick channels via https://7tv.io/v3/users/KICK/{user_id}
    // Note: Returns a "connection" object, not the full user profile
    NetworkRequest(API_URL_USER_KICK.arg(kickUserID), NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess([this, kickUserID, callback = std::move(onSuccess)](
                       const NetworkResult &result) {
            auto json = result.parseJson();

            // Cache the successful result
            {
                std::unique_lock lock(this->kickUserCacheMutex_);
                this->kickUserCache_[kickUserID] =
                    KickUserCacheEntry{json, false};
            }

            callback(json);
        })
        .onError([this, kickUserID,
                  callback = std::move(onError)](const NetworkResult &result) {
            // Cache 404s as "not found" to avoid re-requesting
            if (result.status() == 404)
            {
                std::unique_lock lock(this->kickUserCacheMutex_);
                this->kickUserCache_[kickUserID] =
                    KickUserCacheEntry{std::nullopt, false};
            }
            else
            {
                // Other errors - remove from cache so we can retry later
                std::unique_lock lock(this->kickUserCacheMutex_);
                this->kickUserCache_.erase(kickUserID);
            }

            callback(result);
        })
        .execute();
}

void SeventvAPI::getUserByID(const QString &seventvUserID,
                             SuccessCallback<const QJsonObject &> &&onSuccess,
                             ErrorCallback &&onError)
{
    // Direct 7TV user lookup: https://7tv.io/v3/users/{user_id}
    // Returns full user profile including style with paint
    QString url =
        QStringLiteral("https://7tv.io/v3/users/%1").arg(seventvUserID);
    NetworkRequest(url, NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::getCosmetics(const QStringList &ids,
                              SuccessCallback<const QJsonObject &> &&onSuccess,
                              ErrorCallback &&onError)
{
    // Build the GraphQL query with variables
    // 7TV GraphQL API expects: query Cosmetics($list: [ObjectID!]) { cosmetics(list: $list) { ... } }
    QJsonObject variables;
    QJsonArray idArray;
    for (const auto &id : ids)
    {
        idArray.append(id);
    }
    variables["list"] = idArray;

    QJsonObject requestBody;
    requestBody["query"] = QStringLiteral(
        "query Cosmetics($list: [ObjectID!]) { "
        "cosmetics(list: $list) { "
        "badges { id name tooltip host { url files { name format width height "
        "} } } "
        "paints { id name function color angle repeat stops { at color } "
        "shadows { x_offset y_offset radius color } image_url } "
        "} }");
    requestBody["variables"] = variables;

    NetworkRequest(u"https://7tv.io/v3/gql"_s, NetworkRequestType::Post)
        .timeout(20000)
        .header("Content-Type", "application/json")
        .json(requestBody)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::getEmoteSet(const QString &emoteSet,
                             SuccessCallback<const QJsonObject &> &&onSuccess,
                             ErrorCallback &&onError)
{
    NetworkRequest(API_URL_EMOTE_SET.arg(emoteSet), NetworkRequestType::Get)
        .timeout(25000)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::updatePresence(const QString &twitchChannelID,
                                const QString &seventvUserID,
                                SuccessCallback<> &&onSuccess,
                                ErrorCallback &&onError)
{
    QJsonObject payload{
        {u"kind"_s, 1},  // UserPresenceKindChannel
        {u"data"_s,
         QJsonObject{
             {u"id"_s, twitchChannelID},
             {u"platform"_s, u"TWITCH"_s},
         }},
    };

    NetworkRequest(API_URL_PRESENCES.arg(seventvUserID),
                   NetworkRequestType::Post)
        .json(payload)
        .timeout(10000)
        .onSuccess([callback = std::move(onSuccess)](const auto &) {
            callback();
        })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

}  // namespace chatterino
// NOLINTEND(readability-convert-member-functions-to-static)
