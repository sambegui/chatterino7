#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickBadges.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/kick/KickWebSocket.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "singletons/Settings.hpp"

#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace chatterino {

KickChannel::KickChannel(const QString &channelSlug)
    : Channel(channelSlug, Channel::Type::Kick)
    , channelSlug_(channelSlug)
    , connectionState_(KickConnectionState::Disconnected)
    , nativeKickEmotes_(std::make_shared<EmoteMap>())
    , availableKickEmotes_(std::make_shared<EmoteMap>())
{
}

KickChannel::~KickChannel()
{
    this->disconnect();
}

void KickChannel::sendMessage(const QString &message)
{
    // a handled command sends nothing, so an empty message is not an error
    if (message.trimmed().isEmpty())
    {
        return;
    }

    // Check for message length (Kick limit is 500 characters)
    if (message.length() > 500)
    {
        this->addSystemMessage(translateKickError(KickError::MessageTooLong));
        return;
    }

    if (!this->canSendMessage())
    {
        if (!this->isAuthenticated_)
        {
            this->addSystemMessage(
                translateKickError(KickError::UserMissingScope));
        }
        else if (this->connectionState_ != KickConnectionState::Connected)
        {
            this->addSystemMessage(
                translateKickError(KickError::ConnectionFailed));
        }
        else
        {
            this->addSystemMessage(
                "Cannot send message: channel not ready. Please wait.");
        }
        return;
    }

    if (!this->api_)
    {
        this->addSystemMessage(
            "Cannot send message: API not initialized. Try reconnecting.");
        return;
    }

    if (this->broadcasterUserId_ == 0)
    {
        this->addSystemMessage(translateKickError(KickError::ChannelNotFound));
        return;
    }

    // Convert native Kick emote names to [emote:ID:NAME] format
    QString processedMessage = this->convertEmotesForSending(message);

    // Send via KickApi using broadcaster_user_id (per Context7 docs)
    this->api_->sendMessage(
        this->broadcasterUserId_, processedMessage,
        [this, message](KickApiResult result) {
            if (result.success)
            {
                // Message sent successfully
                bool sent = true;
                this->sendMessageSignal.invoke(this->channelSlug_, message,
                                               sent);
            }
            else
            {
                // Show the already-translated error message from KickApi
                // (No need to add "Failed to send message:" prefix - the
                // translated message is already user-friendly)
                this->addSystemMessage(result.errorMessage);
            }
        });
}

bool KickChannel::isMod() const
{
    return this->userRole_ == "moderator";
}

bool KickChannel::isVip() const
{
    return this->userRole_ == "vip";
}

bool KickChannel::isBroadcaster() const
{
    if (this->userRole_ == "broadcaster")
    {
        return true;
    }

    // the role field is only returned for an authenticated caller, and the
    // unofficial endpoint does not always honour our token, so fall back to
    // the one case we can always be sure of: this is your own channel
    return this->account_ != nullptr && this->account_->isAuthenticated() &&
           this->account_->getUserName().compare(this->channelSlug_,
                                                 Qt::CaseInsensitive) == 0;
}

bool KickChannel::hasModRights() const
{
    return this->isMod() || this->isBroadcaster();
}

const KickApi::RoomModes &KickChannel::roomModes() const
{
    return this->roomModes_;
}

bool KickChannel::canSendMessage() const
{
    return this->connectionState_ == KickConnectionState::Connected &&
           this->isAuthenticated_ && this->api_ != nullptr;
}

bool KickChannel::isLive() const
{
    return this->isLive_;
}

void KickChannel::setApi(std::shared_ptr<KickApi> api)
{
    this->api_ = std::move(api);
}

std::shared_ptr<KickApi> KickChannel::api() const
{
    return this->api_;
}

void KickChannel::setAccount(std::shared_ptr<KickAccount> account)
{
    this->account_ = std::move(account);
    this->isAuthenticated_ =
        (this->account_ != nullptr && this->account_->isAuthenticated());
}

void KickChannel::connect()
{
    if (this->connectionState_ == KickConnectionState::Connected ||
        this->connectionState_ == KickConnectionState::Connecting)
    {
        return;
    }

    if (!getSettings()->enableKickIntegration)
    {
        this->addSystemMessage(
            "Kick integration is disabled. Enable it in Settings → General → "
            "Kick Integration");
        return;
    }

    this->setConnectionState(KickConnectionState::Connecting);

    // every Kick channel shares one socket, so this may already be up
    auto *server = getApp()->getKickChatServer();
    server->ensureConnected();
    if (server->isConnected())
    {
        this->onServerConnected();
    }
}

void KickChannel::onServerConnected()
{
    this->resolveAndSubscribe();
}

void KickChannel::onServerDisconnected()
{
    this->setConnectionState(KickConnectionState::Disconnected);
    this->addSystemMessage("Disconnected from Kick chat");
}

void KickChannel::onServerError()
{
    this->addSystemMessage(translateKickError(KickError::ConnectionFailed));
    this->handleConnectionError();
}

void KickChannel::disconnect()
{
    // runs from the destructor too, which can outlive the Application
    if (auto *app = tryGetApp())
    {
        app->getKickChatServer()->leave(this);
    }
    this->setConnectionState(KickConnectionState::Disconnected);
}

void KickChannel::reconnect()
{
    this->disconnect();

    // Reset reconnection attempts on manual reconnect
    this->reconnectAttempts_ = 0;

    QTimer::singleShot(1000, [weak{this->weak_from_this()}] {
        if (auto self = std::dynamic_pointer_cast<KickChannel>(weak.lock()))
        {
            self->connect();
        }
    });
}

KickConnectionState KickChannel::getConnectionState() const
{
    return this->connectionState_;
}

QString KickChannel::getChannelSlug() const
{
    return this->channelSlug_;
}

int KickChannel::getChatroomId() const
{
    return this->chatroomId_;
}

int KickChannel::getBroadcasterUserId() const
{
    return this->broadcasterUserId_;
}

void KickChannel::setAuthenticated(bool authenticated)
{
    this->isAuthenticated_ = authenticated;
}

bool KickChannel::isAuthenticated() const
{
    return this->isAuthenticated_;
}

void KickChannel::fetchRecentMessages()
{
    // T081: Kick history API is not publicly available
    // This is documented in research.md as a limitation
    // Display notice to user instead of silently failing
    this->addSystemMessage(
        "📡 Kick: Showing live messages only. Chat history is not available "
        "for Kick channels.");
}

void KickChannel::refreshSevenTVChannelEmotes(bool manualRefresh)
{
    if (this->broadcasterUserId_ == 0)
    {
        qCDebug(chatterinoKick)
            << "Cannot load 7TV emotes: broadcaster ID not resolved";
        if (manualRefresh)
        {
            this->addSystemMessage(
                "Cannot reload 7TV emotes: channel not fully connected.");
        }
        return;
    }

    auto *seventv = getApp()->getSeventvAPI();
    if (!seventv)
    {
        qCWarning(chatterinoKick) << "7TV API not available";
        return;
    }

    qCDebug(chatterinoKick)
        << "Loading 7TV emotes for Kick channel" << this->channelSlug_
        << "user ID:" << this->broadcasterUserId_;

    // Use Kick-specific 7TV endpoint: https://7tv.io/v3/users/KICK/{user_id}
    seventv->getUserByKickID(
        QString::number(this->broadcasterUserId_),
        [this, manualRefresh](const QJsonObject &json) {
            const auto emoteSet = json["emote_set"].toObject();
            const auto parsedEmotes = emoteSet["emotes"].toArray();

            auto emoteMap = seventv::detail::parseEmotes(
                parsedEmotes, SeventvEmoteSetKind::Channel);

            if (!emoteMap.empty())
            {
                this->seventvEmotes_ =
                    std::make_shared<const EmoteMap>(emoteMap);
                qCDebug(chatterinoKick)
                    << "Loaded" << emoteMap.size()
                    << "7TV emotes for Kick channel" << this->channelSlug_;

                if (manualRefresh)
                {
                    this->addSystemMessage(
                        QString("Reloaded %1 7TV channel emotes.")
                            .arg(emoteMap.size()));
                }
            }
            else
            {
                qCDebug(chatterinoKick)
                    << "No 7TV emotes found for Kick channel"
                    << this->channelSlug_;

                if (manualRefresh)
                {
                    this->addSystemMessage(
                        "No 7TV channel emotes found for this channel.");
                }
            }
        },
        [this, manualRefresh](const auto &result) {
            qCDebug(chatterinoKick)
                << "Failed to load 7TV emotes for Kick channel"
                << this->channelSlug_ << ":" << result.formatError();

            if (manualRefresh)
            {
                this->addSystemMessage("Failed to reload 7TV channel emotes.");
            }
        });
}

void KickChannel::refreshKickChannelEmotes(bool manualRefresh)
{
    if (this->broadcasterUserId_ == 0)
    {
        qCDebug(chatterinoKick)
            << "Cannot load Kick emotes: broadcaster ID not resolved";
        if (manualRefresh)
        {
            this->addSystemMessage(
                "Cannot reload Kick emotes: channel not fully connected.");
        }
        return;
    }

    // Re-fetch available emotes from Kick API
    this->fetchAvailableEmotes([this, manualRefresh](bool success, int count) {
        if (success)
        {
            if (manualRefresh)
            {
                this->addSystemMessage(
                    QString("Reloaded %1 Kick channel emotes.").arg(count));
            }
        }
        else
        {
            if (manualRefresh)
            {
                this->addSystemMessage("Failed to reload Kick channel emotes.");
            }
        }
    });
}

std::shared_ptr<const EmoteMap> KickChannel::getSeventvEmotes() const
{
    return this->seventvEmotes_;
}

std::shared_ptr<const EmoteMap> KickChannel::getNativeKickEmotes() const
{
    std::shared_lock lock(this->nativeEmotesMutex_);
    return this->nativeKickEmotes_;
}

std::shared_ptr<const EmoteMap> KickChannel::getAvailableKickEmotes() const
{
    std::shared_lock lock(this->availableEmotesMutex_);
    return this->availableKickEmotes_;
}

bool KickChannel::hasSubscriberAccess() const
{
    return this->hasSubscriberAccess_;
}

void KickChannel::fetchAvailableEmotes(
    std::function<void(bool success, int count)> callback)
{
    if (!this->api_)
    {
        qCWarning(chatterinoKick)
            << "Cannot fetch emotes - API not initialized";
        if (callback)
        {
            callback(false, 0);
        }
        return;
    }

    qCDebug(chatterinoKick)
        << "Fetching available emotes for channel:" << this->channelSlug_;

    // First, check user's role in the channel
    this->api_->fetchUserRoleInChannel(
        this->channelSlug_,
        [this, callback](KickApi::UserRoleResult roleResult) {
            if (!roleResult.success)
            {
                qCWarning(chatterinoKick)
                    << "Failed to fetch user role:" << roleResult.errorMessage;
                // Continue anyway - assume no subscriber access
            }

            this->hasSubscriberAccess_ = roleResult.isSubscribed;
            qCDebug(chatterinoKick)
                << "User has subscriber access:" << this->hasSubscriberAccess_
                << "(role:" << roleResult.role << ")";

            // Now fetch channel emotes
            this->api_->fetchChannelEmotes(
                this->channelSlug_,
                [this, callback](KickApi::ChannelEmotesResult emotesResult) {
                    if (!emotesResult.success)
                    {
                        qCWarning(chatterinoKick) << "Failed to fetch emotes:"
                                                  << emotesResult.errorMessage;
                        if (callback)
                        {
                            callback(false, 0);
                        }
                        return;
                    }

                    // Filter emotes based on subscription status
                    auto availableEmotes = std::make_shared<EmoteMap>();
                    int totalEmotes = emotesResult.emotes.size();
                    int availableCount = 0;

                    for (const auto &emoteInfo : emotesResult.emotes)
                    {
                        // Include emote if:
                        // 1. It's not subscriber-only, OR
                        // 2. User has subscriber access
                        if (!emoteInfo.subscribersOnly ||
                            this->hasSubscriberAccess_)
                        {
                            // Create emote URL
                            QString emoteUrl =
                                QString(
                                    "https://files.kick.com/emotes/%1/fullsize")
                                    .arg(emoteInfo.id);

                            // Create the emote with proper sizing
                            static const QSize KICK_EMOTE_BASE_SIZE(28, 28);
                            auto emote = std::make_shared<Emote>(Emote{
                                EmoteName{emoteInfo.name},
                                ImageSet{Image::fromUrl({emoteUrl}, 1.0,
                                                        KICK_EMOTE_BASE_SIZE)},
                                Tooltip{QString("%1 Kick Emote")
                                            .arg(emoteInfo.name)},
                                Url{emoteUrl},
                                false,  // not zero-width
                                EmoteId{QString::number(emoteInfo.id)},
                            });

                            availableEmotes->emplace(EmoteName{emoteInfo.name},
                                                     emote);
                            availableCount++;
                        }
                    }

                    // Store the available emotes
                    {
                        std::unique_lock lock(this->availableEmotesMutex_);
                        this->availableKickEmotes_ = availableEmotes;
                        this->emotesLoaded_ = true;
                    }

                    qCDebug(chatterinoKick)
                        << "Loaded" << availableCount << "of" << totalEmotes
                        << "emotes for channel" << this->channelSlug_
                        << "(subscriber access:" << this->hasSubscriberAccess_
                        << ")";

                    // Emit signal that emotes are updated
                    this->availableEmotesUpdated.invoke();

                    if (callback)
                    {
                        callback(true, availableCount);
                    }
                });
        });
}

void KickChannel::onMessageReceived(const KickMessage &kickMessage)
{
    MessageBuilder builder;

    // Set timestamp from Kick message
    builder.message().serverReceivedTime = kickMessage.createdAt;

    // Add Kick platform indicator (for merged channel support)
    builder.message().flags.set(MessageFlag::Kick);

    // Build username element with color
    builder.emplace<TimestampElement>(kickMessage.createdAt.time());

    // Add badges (if any)
    for (const auto &badge : kickMessage.sender.identity.badges)
    {
        auto [emote, flag] = KickBadges::lookup(badge.type);
        if (emote)
        {
            builder.emplace<BadgeElement>(emote, flag);
        }
    }

    // Load 7TV cosmetics (paints, badges) for this user if we haven't already
    if (kickMessage.sender.id > 0)
    {
        this->loadUserSevenTVCosmetics(kickMessage.sender.id,
                                       kickMessage.sender.username);
    }

    // Append 7TV badge for this user (if they have one assigned)
    QString kickUserIdStr = QString::number(kickMessage.sender.id);
    if (auto badge =
            getApp()->getSeventvBadges()->getBadge(UserId{kickUserIdStr}))
    {
        builder.emplace<BadgeElement>(*badge, MessageElementFlag::BadgeSevenTV);
    }

    // Add username with color
    QString usernameText = kickMessage.sender.username;
    QColor userColor;
    if (!kickMessage.sender.identity.color.isEmpty())
    {
        userColor = QColor(kickMessage.sender.identity.color);
    }
    else
    {
        // Default color for users without a set color
        userColor = QColor(155, 89, 182);  // Purple default
    }

    builder
        .emplace<TextElement>(usernameText + ":", MessageElementFlag::Username,
                              MessageColor(userColor),
                              FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, kickMessage.sender.username});

    // Add message content with emote parsing
    // Kick emotes are in format: [emote:ID:NAME]
    // Example: "Hello [emote:4148074:HYPERCLAP] world"
    this->parseMessageContent(builder, kickMessage.content, kickMessage.emotes);

    // Store Kick-specific metadata
    builder.message().loginName = kickMessage.sender.slug;
    builder.message().displayName = kickMessage.sender.username;

    auto message = builder.release();
    this->addMessage(message, MessageContext::Original);
}

void KickChannel::loadUserSevenTVCosmetics(int kickUserId,
                                           const QString &userName)
{
    // Check if we've already loaded cosmetics for this user
    {
        std::shared_lock lock(this->loadedUsersMutex_);
        if (this->usersWithLoadedCosmetics_.contains(kickUserId))
        {
            return;  // Already loaded
        }
    }

    // Mark as loading (even before the request completes to avoid duplicates)
    {
        std::unique_lock lock(this->loadedUsersMutex_);
        this->usersWithLoadedCosmetics_.insert(kickUserId);
    }

    // 7TV Kick endpoint returns a "connection" object, not the full user.
    // The connection contains "emote_set_id" which is the 7TV user ID.
    // We need a two-step lookup:
    // 1. GET /v3/users/KICK/{kick_id} -> get emote_set_id (7TV user ID)
    // 2. GET /v3/users/{7tv_user_id} -> get full profile with paint
    getApp()->getSeventvAPI()->getUserByKickID(
        QString::number(kickUserId),
        [userName, kickUserId](const QJsonObject &connectionJson) {
            // The /v3/users/KICK/{id} endpoint returns a connection object
            // The actual 7TV user is nested in the "user" field
            QJsonObject userObj = connectionJson["user"].toObject();
            QString seventvUserId = userObj["id"].toString();

            if (seventvUserId.isEmpty())
            {
                qCDebug(chatterinoKick)
                    << "No 7TV user ID found for Kick user" << kickUserId;
                return;
            }

            qCDebug(chatterinoKick) << "Found 7TV user ID" << seventvUserId
                                    << "for Kick user" << userName;

            // The connection response already includes full user data with cosmetics
            // in the "user" field, so we can directly load cosmetics from it
            auto *paints = getApp()->getSeventvPaints();
            if (paints)
            {
                // Pass Kick user ID for badge assignment
                paints->loadUserCosmetics(userObj, userName,
                                          QString::number(kickUserId));
            }
        },
        [kickUserId](const NetworkResult &result) {
            // Not an error if 7TV doesn't have this user
            if (result.status() != 404)
            {
                qCDebug(chatterinoKick)
                    << "Failed to load 7TV connection for Kick user"
                    << kickUserId << ":" << result.formatError();
            }
        });
}

void KickChannel::setConnectionState(KickConnectionState state)
{
    if (this->connectionState_ != state)
    {
        this->connectionState_ = state;
        this->connectionStateChanged.invoke(state);
    }
}

void KickChannel::resolveAndSubscribe()
{
    // Emit connecting message
    this->addSystemMessage(
        QString("Connecting to Kick channel: %1...").arg(this->channelSlug_));

    // Create API instance if needed (for channel resolution)
    if (!this->api_)
    {
        this->api_ = std::make_shared<KickApi>();
    }

    // Use KickApi to resolve channel slug to chatroom ID
    this->api_->resolveChannelInfo(
        this->channelSlug_, [this](KickApi::ChannelInfo info) {
            if (!info.success)
            {
                this->setConnectionState(KickConnectionState::Failed);
                this->addSystemMessage(
                    translateKickError(KickError::ChannelNotFound));
                return;
            }

            this->chatroomId_ = info.chatroomId;
            this->broadcasterUserId_ = info.broadcasterUserId;
            this->userRole_ = info.userRole;

            if (this->roomModes_.subscribersOnly !=
                    info.roomModes.subscribersOnly ||
                this->roomModes_.emotesOnly != info.roomModes.emotesOnly ||
                this->roomModes_.slowModeInterval !=
                    info.roomModes.slowModeInterval ||
                this->roomModes_.followersOnlyDuration !=
                    info.roomModes.followersOnlyDuration)
            {
                this->roomModes_ = info.roomModes;
                this->roomModesChanged.invoke();
            }

            // Update live status
            bool wasLive = this->isLive_;
            this->isLive_ = info.isLive;
            this->streamTitle_ = info.streamTitle;
            this->viewerCount_ = info.viewerCount;

            if (wasLive != this->isLive_)
            {
                this->liveStatusChanged.invoke();
            }

            qCDebug(chatterinoKick)
                << "Channel" << this->channelSlug_ << "resolved:"
                << "chatroomId=" << this->chatroomId_
                << "broadcasterUserId=" << this->broadcasterUserId_
                << "isLive=" << this->isLive_;

            auto *server = getApp()->getKickChatServer();
            if (server->isConnected())
            {
                server->subscribeChatroom(
                    this->chatroomId_, std::dynamic_pointer_cast<KickChannel>(
                                           this->shared_from_this()));
                this->setConnectionState(KickConnectionState::Connected);

                QString statusMsg = QString("Connected to Kick channel: %1")
                                        .arg(this->channelSlug_);
                if (this->isLive_)
                {
                    statusMsg +=
                        QString(" [LIVE - %1 viewers]").arg(this->viewerCount_);
                }
                this->addSystemMessage(statusMsg);

                // Fetch available emotes for this channel
                this->fetchAvailableEmotes();

                // Load 7TV emotes for this Kick channel
                this->refreshSevenTVChannelEmotes();
            }
            else
            {
                this->setConnectionState(KickConnectionState::Failed);
                this->addSystemMessage(
                    "WebSocket disconnected during channel resolution");
            }
        });
}

void KickChannel::handleConnectionError()
{
    if (this->connectionState_ == KickConnectionState::Reconnecting)
    {
        // Already trying to reconnect
        return;
    }

    this->setConnectionState(KickConnectionState::Reconnecting);
    this->scheduleReconnect();
}

void KickChannel::scheduleReconnect()
{
    if (this->reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS)
    {
        this->setConnectionState(KickConnectionState::Failed);
        this->addSystemMessage(translateKickError(KickError::ConnectionFailed) +
                               " Use the reconnect option to try again.");
        return;
    }

    // Exponential backoff with jitter (matching Twitch EventSub pattern)
    // Base delay: 1s, 2s, 4s, 8s, 16s, 30s (capped)
    int baseDelayMs = std::min(1000 * (1 << this->reconnectAttempts_), 30000);

    // Add random jitter (0-25% of base delay) to prevent thundering herd
    int jitterMs = QRandomGenerator::global()->bounded(baseDelayMs / 4);
    int delayMs = baseDelayMs + jitterMs;

    this->reconnectAttempts_++;

    this->addSystemMessage(
        QStringLiteral("Reconnecting to Kick in %1 seconds... (attempt %2/%3)")
            .arg(delayMs / 1000)
            .arg(this->reconnectAttempts_)
            .arg(MAX_RECONNECT_ATTEMPTS));

    QTimer::singleShot(delayMs, [weak{this->weak_from_this()}] {
        auto self = std::dynamic_pointer_cast<KickChannel>(weak.lock());
        if (!self)
        {
            return;
        }

        if (self->connectionState_ == KickConnectionState::Reconnecting)
        {
            self->addSystemMessage(QStringLiteral("Reconnecting..."));
            self->disconnect();
            self->connect();
        }
    });
}

std::optional<EmotePtr> KickChannel::findThirdPartyEmote(
    const QString &word) const
{
    // Check 7TV channel emotes first
    if (this->seventvEmotes_)
    {
        auto it = this->seventvEmotes_->find(EmoteName{word});
        if (it != this->seventvEmotes_->end())
        {
            return it->second;
        }
    }

    // Check global 7TV emotes
    auto *globalSeventv = getApp()->getSeventvEmotes();
    if (globalSeventv)
    {
        auto emote = globalSeventv->globalEmote(EmoteName{word});
        if (emote)
        {
            return emote;
        }
    }

    // Check global BTTV emotes
    auto *globalBttv = getApp()->getBttvEmotes();
    if (globalBttv)
    {
        auto emote = globalBttv->emote(EmoteName{word});
        if (emote)
        {
            return emote;
        }
    }

    // Check global FFZ emotes
    auto *globalFfz = getApp()->getFfzEmotes();
    if (globalFfz)
    {
        auto emote = globalFfz->emote(EmoteName{word});
        if (emote)
        {
            return emote;
        }
    }

    return std::nullopt;
}

void KickChannel::addTextOrEmote(MessageBuilder &builder,
                                 const QString &text) const
{
    // Split text into words and check each for emotes
    QStringList words = text.split(' ', Qt::SkipEmptyParts);

    for (int i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];

        // Check if this word is a third-party emote
        auto emote = this->findThirdPartyEmote(word);
        if (emote)
        {
            builder.emplace<EmoteElement>(*emote, MessageElementFlag::Emote);
        }
        else
        {
            // Just regular text
            builder.emplace<TextElement>(word, MessageElementFlag::Text,
                                         MessageColor::Text);
        }
    }
}

QString KickChannel::convertEmotesForSending(const QString &message) const
{
    // Convert native Kick emote names to [emote:ID:NAME] format
    // This is required for Kick to recognize and render emotes in chat

    // Get native Kick emotes from this channel
    std::shared_ptr<const EmoteMap> channelEmotes;
    {
        std::shared_lock lock(this->nativeEmotesMutex_);
        channelEmotes = this->nativeKickEmotes_;
    }

    // Get available Kick emotes (from API, filtered by access)
    std::shared_ptr<const EmoteMap> availableEmotes;
    {
        std::shared_lock lock(this->availableEmotesMutex_);
        availableEmotes = this->availableKickEmotes_;
    }

    // Get global Kick emotes
    auto globalEmotes = getApp()->getKickEmotes()->globalEmotes();

    // If no emotes available at all, return unchanged
    bool hasChannelEmotes = channelEmotes && !channelEmotes->empty();
    bool hasAvailableEmotes = availableEmotes && !availableEmotes->empty();
    bool hasGlobalEmotes = globalEmotes && !globalEmotes->empty();

    if (!hasChannelEmotes && !hasAvailableEmotes && !hasGlobalEmotes)
    {
        return message;
    }

    // Split message into words and check each against known emotes
    QStringList words = message.split(' ');
    for (int i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];
        EmotePtr foundEmote = nullptr;

        // First check native channel emotes (accumulated from messages)
        if (hasChannelEmotes)
        {
            auto it = channelEmotes->find(EmoteName{word});
            if (it != channelEmotes->end())
            {
                foundEmote = it->second;
            }
        }

        // Then check available emotes (from API)
        if (!foundEmote && hasAvailableEmotes)
        {
            auto it = availableEmotes->find(EmoteName{word});
            if (it != availableEmotes->end())
            {
                foundEmote = it->second;
            }
        }

        // Then check global emotes if not found
        if (!foundEmote && hasGlobalEmotes)
        {
            auto it = globalEmotes->find(EmoteName{word});
            if (it != globalEmotes->end())
            {
                foundEmote = it->second;
            }
        }

        if (foundEmote)
        {
            // Convert to [emote:ID:NAME] format
            QString emoteId = foundEmote->id.string;
            if (!emoteId.isEmpty())
            {
                words[i] = QString("[emote:%1:%2]").arg(emoteId, word);
            }
        }
    }

    return words.join(' ');
}

void KickChannel::parseMessageContent(MessageBuilder &builder,
                                      const QString &content,
                                      const std::vector<KickEmote> &emotes)
{
    // Regex to match [emote:ID:NAME] pattern (fallback if API positions don't work)
    // Format: [emote:37218:Clap] where 37218 is the emote ID and Clap is the name
    static const QRegularExpression emoteRegex(R"(\[emote:(\d+):([^\]]+)\])");

    // First, try to use emotes from API if they have valid positions
    bool hasValidApiEmotes = false;
    for (const auto &emote : emotes)
    {
        if (!emote.positions.empty() && !emote.emoteId.isEmpty())
        {
            hasValidApiEmotes = true;
            break;
        }
    }

    // If API provided valid emote positions, use them
    if (hasValidApiEmotes)
    {
        struct EmoteRange {
            int start;
            int end;
            QString emoteId;
            QString emoteName;
        };
        std::vector<EmoteRange> ranges;

        for (const auto &emote : emotes)
        {
            for (const auto &pos : emote.positions)
            {
                if (pos.start >= 0 && pos.end > pos.start &&
                    pos.end <= content.length())
                {
                    EmoteRange range;
                    range.start = pos.start;
                    range.end = pos.end;
                    range.emoteId = emote.emoteId;
                    range.emoteName = emote.emoteName;

                    // If no name from API, try to extract from content
                    if (range.emoteName.isEmpty())
                    {
                        QString emoteText =
                            content.mid(pos.start, pos.end - pos.start);
                        // Check for [emote:ID:NAME] format
                        if (emoteText.startsWith("[emote:") &&
                            emoteText.endsWith("]"))
                        {
                            QStringList parts =
                                emoteText.mid(7, emoteText.length() - 8)
                                    .split(':');
                            if (parts.size() >= 2)
                            {
                                range.emoteName = parts.last();
                            }
                        }
                        else
                        {
                            // Use the text itself as the name
                            range.emoteName = emoteText.trimmed();
                        }
                    }

                    ranges.push_back(range);
                }
            }
        }

        // Sort by start position
        std::sort(ranges.begin(), ranges.end(),
                  [](const EmoteRange &a, const EmoteRange &b) {
                      return a.start < b.start;
                  });

        if (!ranges.empty())
        {
            int currentPos = 0;
            for (const auto &range : ranges)
            {
                // Add text before this emote
                if (range.start > currentPos)
                {
                    QString textBefore =
                        content.mid(currentPos, range.start - currentPos)
                            .trimmed();
                    if (!textBefore.isEmpty())
                    {
                        this->addTextOrEmote(builder, textBefore);
                    }
                }

                // Add Kick emote with proper ImageSet (matching Twitch sizing)
                QString emoteUrl =
                    QString("https://files.kick.com/emotes/%1/fullsize")
                        .arg(range.emoteId);

                EmoteId emoteId{range.emoteId};
                EmoteName emoteName{range.emoteName.isEmpty()
                                        ? range.emoteId
                                        : range.emoteName};

                // Create ImageSet with single image for Kick emotes
                // Kick provides a single high-res image - we create ONE Image and let
                // the system scale it. Using same URL for multiple Image::fromUrl calls
                // returns the same cached object anyway, so we only need the 1x version.
                constexpr QSize BASE_EMOTE_SIZE(28, 28);

                auto emote = std::make_shared<Emote>(Emote{
                    emoteName,
                    ImageSet{Image::fromUrl({emoteUrl}, 1.0, BASE_EMOTE_SIZE)},
                    Tooltip{emoteName.string + "<br>Kick Emote"},
                    Url{emoteUrl},
                    false,
                    emoteId,
                });

                // Store emote for autocomplete (thread-safe)
                {
                    std::unique_lock lock(this->nativeEmotesMutex_);
                    if (this->nativeKickEmotes_->find(emoteName) ==
                        this->nativeKickEmotes_->end())
                    {
                        (*this->nativeKickEmotes_)[emoteName] = emote;
                    }
                }

                builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);
                currentPos = range.end;
            }

            // Add remaining text
            if (currentPos < content.length())
            {
                QString remainingText = content.mid(currentPos).trimmed();
                if (!remainingText.isEmpty())
                {
                    this->addTextOrEmote(builder, remainingText);
                }
            }
            return;  // Successfully processed with API emotes
        }
    }

    // Fallback: Scan content with regex for [emote:ID:NAME] pattern

    // Find all native Kick emotes in the content
    struct EmoteMatch {
        int start;
        int end;
        QString emoteId;
        QString emoteName;
    };
    std::vector<EmoteMatch> matches;

    QRegularExpressionMatchIterator it = emoteRegex.globalMatch(content);
    while (it.hasNext())
    {
        QRegularExpressionMatch match = it.next();
        EmoteMatch em;
        em.start = match.capturedStart();
        em.end = match.capturedEnd();
        em.emoteId = match.captured(1);    // The numeric ID
        em.emoteName = match.captured(2);  // The emote name
        matches.push_back(em);
    }

    // Build message elements
    int currentPos = 0;
    for (const auto &em : matches)
    {
        // Add text before this emote (check for third-party emotes)
        if (em.start > currentPos)
        {
            QString textBefore = content.mid(currentPos, em.start - currentPos);
            // Trim but preserve spacing intent
            QString trimmed = textBefore.trimmed();
            if (!trimmed.isEmpty())
            {
                this->addTextOrEmote(builder, trimmed);
            }
        }

        // Add native Kick emote element with single-image ImageSet
        // Kick emote CDN URL: https://files.kick.com/emotes/{emote_id}/fullsize
        QString emoteUrl = QString("https://files.kick.com/emotes/%1/fullsize")
                               .arg(em.emoteId);

        EmoteId emoteId{em.emoteId};
        EmoteName emoteName{em.emoteName};

        // Create ImageSet with single image - Kick provides one high-res image
        // The system will scale it as needed for different display densities
        constexpr QSize BASE_EMOTE_SIZE(28, 28);
        auto emote = std::make_shared<Emote>(Emote{
            emoteName,
            ImageSet{Image::fromUrl({emoteUrl}, 1.0, BASE_EMOTE_SIZE)},
            Tooltip{emoteName.string + "<br>Kick Emote"},
            Url{emoteUrl},
            false,  // Not zero-width
            emoteId,
        });

        // Store emote for autocomplete (thread-safe)
        {
            std::unique_lock lock(this->nativeEmotesMutex_);
            if (this->nativeKickEmotes_->find(emoteName) ==
                this->nativeKickEmotes_->end())
            {
                (*this->nativeKickEmotes_)[emoteName] = emote;
            }
        }

        builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);

        currentPos = em.end;
    }

    // Handle remaining content after last emote (or whole content if no emotes)
    if (currentPos < content.length())
    {
        QString remainingText = content.mid(currentPos).trimmed();
        if (!remainingText.isEmpty())
        {
            this->addTextOrEmote(builder, remainingText);
        }
    }
    else if (matches.empty() && !content.isEmpty())
    {
        // No native emotes found, process whole content for third-party emotes
        this->addTextOrEmote(builder, content);
    }
}

}  // namespace chatterino
