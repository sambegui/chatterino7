#include "channels/MergedChannel.hpp"

#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageFlag.hpp"

#include <QPixmap>

#include <algorithm>
#include <chrono>

namespace chatterino {

namespace {

/// One scale for every platform badge, so they cannot drift apart again.
///
/// Image::size() clamps to expectedSize_ (16x16 by default) whenever the source
/// is larger, and these are 48px, so the rendered size is 16 * scale, not
/// 48 * scale. 16 * 0.6875 = 11px, against the 8px Twitch used to get and the
/// 5px Kick did.
///
/// The pixmaps must be static: Image::fromResourcePixmap caches on the
/// pixmap's address, so two stack temporaries sharing an address and a scale
/// collide and hand back each other's image.
constexpr qreal PLATFORM_BADGE_SCALE = 0.6875;

}  // namespace

MergedChannel::MergedChannel(const QString &name,
                             std::vector<ChannelPtr> sourceChannels)
    : Channel(name, Channel::Type::Merged)
    , sourceChannels_(std::move(sourceChannels))
{
    // Subscribe to all source channels
    for (const auto &channel : this->sourceChannels_)
    {
        this->subscribeToChannel(channel);
    }
}

MergedChannel::~MergedChannel()
{
    this->signalHolder_.clear();
}

void MergedChannel::sendMessage(const QString &message)
{
    if (!this->canSendMessage())
    {
        // T077: Show error when sending to unauthenticated platform
        this->addSystemMessage(
            "Cannot send message. Please ensure you are logged in to the "
            "selected platform(s).");
        return;
    }

    // Track results for each platform
    struct SendResult {
        Channel::Type type;
        bool attempted{false};
        bool canSend{false};
        QString error;
    };
    std::vector<SendResult> results;
    int platformsSentTo = 0;

    // Send to selected platforms
    for (const auto &channel : this->sourceChannels_)
    {
        bool shouldSend = false;

        switch (this->platformSelection_)
        {
            case PlatformSelection::Both:
                shouldSend = true;
                break;
            case PlatformSelection::TwitchOnly:
                shouldSend = (channel->getType() == Channel::Type::Twitch);
                break;
            case PlatformSelection::KickOnly:
                shouldSend = (channel->getType() == Channel::Type::Kick);
                break;
            case PlatformSelection::YouTubeOnly:
                shouldSend = (channel->getType() == Channel::Type::YouTube);
                break;
        }

        if (shouldSend)
        {
            SendResult result;
            result.type = channel->getType();
            result.attempted = true;
            result.canSend = channel->canSendMessage();

            if (result.canSend)
            {
                // Attempt to send
                channel->sendMessage(message);
                platformsSentTo++;
            }
            else
            {
                // T077: Show error for unauthenticated platform
                result.error = "Not authenticated";
            }

            results.push_back(result);
        }
    }

    // Track sent message for deduplication when sent to both platforms
    if (platformsSentTo > 1)
    {
        PendingSentMessage pending;
        pending.messageText = message;
        pending.sentTime = std::chrono::steady_clock::now();
        pending.receivedFromPlatforms = 0;
        this->pendingSentMessages_.push_back(pending);

        // Clean up old pending messages (older than 10 seconds)
        auto now = std::chrono::steady_clock::now();
        this->pendingSentMessages_.erase(
            std::remove_if(
                this->pendingSentMessages_.begin(),
                this->pendingSentMessages_.end(),
                [now](const PendingSentMessage &msg) {
                    return std::chrono::duration_cast<std::chrono::seconds>(
                               now - msg.sentTime)
                               .count() > 10;
                }),
            this->pendingSentMessages_.end());
    }

    // T072/T076: Display per-platform send status
    QStringList statusParts;
    for (const auto &result : results)
    {
        QString platform = getPlatformPrefix(result.type);
        if (!result.attempted)
        {
            continue;
        }

        if (result.canSend)
        {
            statusParts.append(QString("%1✓").arg(platform));
        }
        else
        {
            statusParts.append(QString("%1✗").arg(platform));

            // T076: Show per-platform send errors
            this->addSystemMessage(
                QString("%1: %2")
                    .arg(MergedChannel::platformDisplayName(result.type))
                    .arg(result.error));
        }
    }

    // Log results
    if (!statusParts.isEmpty())
    {
        qCDebug(chatterinoCommon)
            << "Message send status:" << statusParts.join(" ");
    }
}

bool MergedChannel::isMod() const
{
    // Return true if mod in any source channel
    for (const auto &channel : this->sourceChannels_)
    {
        if (channel->isMod())
        {
            return true;
        }
    }
    return false;
}

bool MergedChannel::canSendMessage() const
{
    // Can send if any source channel allows it based on platform selection
    for (const auto &channel : this->sourceChannels_)
    {
        bool matchesPlatform = false;

        switch (this->platformSelection_)
        {
            case PlatformSelection::Both:
                matchesPlatform = true;
                break;
            case PlatformSelection::TwitchOnly:
                matchesPlatform = (channel->getType() == Channel::Type::Twitch);
                break;
            case PlatformSelection::KickOnly:
                matchesPlatform = (channel->getType() == Channel::Type::Kick);
                break;
            case PlatformSelection::YouTubeOnly:
                matchesPlatform =
                    (channel->getType() == Channel::Type::YouTube);
                break;
        }

        if (matchesPlatform && channel->canSendMessage())
        {
            return true;
        }
    }
    return false;
}

const QString &MergedChannel::getDisplayName() const
{
    this->updateCachedDisplayName();
    return this->cachedDisplayName_;
}

void MergedChannel::updateCachedDisplayName() const
{
    QStringList parts;
    for (const auto &channel : this->sourceChannels_)
    {
        QString prefix = getPlatformPrefix(channel->getType());
        parts.append(QString("%1:%2").arg(prefix, channel->getName()));
    }
    this->cachedDisplayName_ = parts.join(" + ");
}

const std::vector<ChannelPtr> &MergedChannel::getSourceChannels() const
{
    return this->sourceChannels_;
}

void MergedChannel::addSourceChannel(ChannelPtr channel)
{
    // Check if already added
    for (const auto &existing : this->sourceChannels_)
    {
        if (existing == channel)
        {
            return;
        }
    }

    this->sourceChannels_.push_back(channel);
    this->subscribeToChannel(channel);
    this->sourceChannelsChanged.invoke();
}

void MergedChannel::removeSourceChannel(const ChannelPtr &channel)
{
    auto it = std::find(this->sourceChannels_.begin(),
                        this->sourceChannels_.end(), channel);
    if (it != this->sourceChannels_.end())
    {
        this->sourceChannels_.erase(it);
        this->sourceChannelsChanged.invoke();
    }
}

std::vector<ChannelPtr> MergedChannel::unmerge()
{
    // Clear signal connections
    this->signalHolder_.clear();

    // Move source channels to return value
    std::vector<ChannelPtr> channels = std::move(this->sourceChannels_);
    this->sourceChannels_.clear();

    // Notify listeners
    this->sourceChannelsChanged.invoke();

    qCDebug(chatterinoCommon) << "Merged channel unmerged, returning"
                              << channels.size() << "channels";

    return channels;
}

PlatformSelection MergedChannel::getPlatformSelection() const
{
    return this->platformSelection_;
}

void MergedChannel::setPlatformSelection(PlatformSelection selection)
{
    if (this->platformSelection_ != selection)
    {
        this->platformSelection_ = selection;
        this->platformSelectionChanged.invoke(selection);
    }
}

bool MergedChannel::hasPlatform(Channel::Type type) const
{
    for (const auto &channel : this->sourceChannels_)
    {
        if (channel->getType() == type)
        {
            return true;
        }
    }
    return false;
}

ChannelPtr MergedChannel::sourceForPlatform(Channel::Type type) const
{
    for (const auto &channel : this->sourceChannels_)
    {
        if (channel->getType() == type)
        {
            return channel;
        }
    }
    return {};
}

void MergedChannel::subscribeToChannel(const ChannelPtr &channel)
{
    Channel::Type sourceType = channel->getType();

    // Subscribe to message appended signal
    this->signalHolder_.managedConnect(
        channel->messageAppended,
        [this, sourceType](MessagePtr &message,
                           std::optional<MessageFlags> overridingFlags) {
            Q_UNUSED(overridingFlags);
            this->onSourceMessageReceived(message, sourceType);
        });
}

void MergedChannel::onSourceMessageReceived(MessagePtr message,
                                            Channel::Type sourceType)
{
    // Check if this is a duplicate of our own sent message (sent to both platforms)
    auto now = std::chrono::steady_clock::now();
    for (auto &pending : this->pendingSentMessages_)
    {
        // Check if message text matches and within time window (10 seconds)
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           now - pending.sentTime)
                           .count();
        if (elapsed <= 10 && message->messageText == pending.messageText)
        {
            pending.receivedFromPlatforms++;

            if (pending.receivedFromPlatforms == 1)
            {
                // First echo - show with "Both" badge
                auto clonedMessage = message->clone();
                bool includesYouTube =
                    this->hasPlatform(Channel::Type::YouTube);
                clonedMessage->flags.set(MessageFlag::Twitch);
                clonedMessage->flags.set(MessageFlag::Kick);
                if (includesYouTube)
                {
                    clonedMessage->flags.set(MessageFlag::YouTube);
                }

                // Add the combined platform badge
                auto bothBadge = makeBothPlatformBadge(includesYouTube);
                if (bothBadge)
                {
                    auto badgeElement = std::make_unique<BadgeElement>(
                        bothBadge, MessageElementFlag::BadgePlatform);

                    size_t insertPos = 0;
                    for (size_t i = 0; i < clonedMessage->elements.size(); i++)
                    {
                        if (clonedMessage->elements[i]->getFlags().has(
                                MessageElementFlag::Timestamp))
                        {
                            insertPos = i + 1;
                            break;
                        }
                    }

                    if (insertPos > 0 &&
                        insertPos <= clonedMessage->elements.size())
                    {
                        clonedMessage->elements.insert(
                            clonedMessage->elements.begin() +
                                static_cast<ptrdiff_t>(insertPos),
                            std::move(badgeElement));
                    }
                    else
                    {
                        clonedMessage->elements.insert(
                            clonedMessage->elements.begin(),
                            std::move(badgeElement));
                    }
                }

                this->addMessage(std::move(clonedMessage),
                                 MessageContext::Original);
                qCDebug(chatterinoCommon)
                    << "Displayed sent message with Both badge";
            }
            else
            {
                // Second echo - suppress duplicate
                qCDebug(chatterinoCommon)
                    << "Suppressed duplicate sent message from"
                    << MergedChannel::platformDisplayName(sourceType);
            }
            return;
        }
    }

    // Normal message processing (not our own sent to both)
    auto clonedMessage = message->clone();

    // Set platform flag for UI display
    if (sourceType == Channel::Type::Twitch)
    {
        clonedMessage->flags.set(MessageFlag::Twitch);
    }
    else if (sourceType == Channel::Type::Kick)
    {
        clonedMessage->flags.set(MessageFlag::Kick);
    }
    else if (sourceType == Channel::Type::YouTube)
    {
        clonedMessage->flags.set(MessageFlag::YouTube);
    }

    // Add platform favicon badge after the timestamp
    auto platformBadge = makePlatformBadge(sourceType);
    if (platformBadge)
    {
        auto badgeElement = std::make_unique<BadgeElement>(
            platformBadge, MessageElementFlag::BadgePlatform);

        // Find the position after the timestamp element
        // Typical order: Timestamp, [Platform Badge], Badges, Username, Text
        size_t insertPos = 0;
        for (size_t i = 0; i < clonedMessage->elements.size(); i++)
        {
            if (clonedMessage->elements[i]->getFlags().has(
                    MessageElementFlag::Timestamp))
            {
                insertPos = i + 1;
                break;
            }
        }

        // Insert the platform badge after timestamp
        if (insertPos > 0 && insertPos <= clonedMessage->elements.size())
        {
            clonedMessage->elements.insert(
                clonedMessage->elements.begin() +
                    static_cast<ptrdiff_t>(insertPos),
                std::move(badgeElement));
        }
        else
        {
            // Fallback: insert at beginning if no timestamp found
            clonedMessage->elements.insert(clonedMessage->elements.begin(),
                                           std::move(badgeElement));
        }
    }

    // Add message chronologically based on serverReceivedTime
    // The Channel::addMessage handles chronological insertion
    this->addMessage(std::move(clonedMessage), MessageContext::Original);
}

QString MergedChannel::getPlatformPrefix(Channel::Type type)
{
    switch (type)
    {
        case Channel::Type::Twitch:
            return "T";
        case Channel::Type::Kick:
            return "K";
        case Channel::Type::YouTube:
            return "Y";
        default:
            return "?";
    }
}

EmotePtr MergedChannel::makePlatformBadge(Channel::Type type)
{
    // Use local favicon resources as platform indicator badges
    // These are small, recognizable icons that users will immediately understand

    switch (type)
    {
        case Channel::Type::Twitch: {
            // Load Twitch favicon from local resources (cached as static)
            static auto twitchEmote = []() -> EmotePtr {
                static const QPixmap pixmap(":/platforms/twitch.png");
                if (pixmap.isNull())
                {
                    qCWarning(chatterinoCommon)
                        << "Failed to load Twitch platform badge from "
                           ":/platforms/twitch.png";
                    return nullptr;
                }
                auto image =
                    Image::fromResourcePixmap(pixmap, PLATFORM_BADGE_SCALE);
                return std::make_shared<Emote>(Emote{
                    .name = EmoteName{"[Twitch]"},
                    .images = ImageSet{image},
                    .tooltip = Tooltip{"Message from Twitch"},
                    .homePage = Url{"https://www.twitch.tv"},
                });
            }();
            return twitchEmote;
        }
        case Channel::Type::Kick: {
            // Load Kick favicon from local resources (cached as static)
            static auto kickEmote = []() -> EmotePtr {
                static const QPixmap pixmap(":/platforms/kick.png");
                if (pixmap.isNull())
                {
                    qCWarning(chatterinoCommon)
                        << "Failed to load Kick platform badge from "
                           ":/platforms/kick.png";
                    return nullptr;
                }
                auto image =
                    Image::fromResourcePixmap(pixmap, PLATFORM_BADGE_SCALE);
                return std::make_shared<Emote>(Emote{
                    .name = EmoteName{"[Kick]"},
                    .images = ImageSet{image},
                    .tooltip = Tooltip{"Message from Kick"},
                    .homePage = Url{"https://kick.com"},
                });
            }();
            return kickEmote;
        }
        case Channel::Type::YouTube: {
            static auto youtubeEmote = []() -> EmotePtr {
                static const QPixmap pixmap(":/platforms/youtube.png");
                if (pixmap.isNull())
                {
                    qCWarning(chatterinoCommon)
                        << "Failed to load YouTube platform badge from "
                           ":/platforms/youtube.png";
                    return nullptr;
                }
                auto image =
                    Image::fromResourcePixmap(pixmap, PLATFORM_BADGE_SCALE);
                return std::make_shared<Emote>(Emote{
                    .name = EmoteName{"[YouTube]"},
                    .images = ImageSet{image},
                    .tooltip = Tooltip{"Message from YouTube"},
                    .homePage = Url{"https://www.youtube.com"},
                });
            }();
            return youtubeEmote;
        }
        default:
            return nullptr;
    }
}

QString MergedChannel::platformDisplayName(Channel::Type type)
{
    switch (type)
    {
        case Channel::Type::Twitch:
            return QStringLiteral("Twitch");
        case Channel::Type::Kick:
            return QStringLiteral("Kick");
        case Channel::Type::YouTube:
            return QStringLiteral("YouTube");
        default:
            return QStringLiteral("Unknown");
    }
}

EmotePtr MergedChannel::makeBothPlatformBadge(bool includesYouTube)
{
    if (includesYouTube)
    {
        static auto allEmote = []() -> EmotePtr {
            static const QPixmap pixmap(":/platforms/allplatforms.png");
            if (pixmap.isNull())
            {
                qCWarning(chatterinoCommon)
                    << "Failed to load combined platform badge from "
                       ":/platforms/allplatforms.png";
                return nullptr;
            }
            auto image =
                Image::fromResourcePixmap(pixmap, PLATFORM_BADGE_SCALE);
            return std::make_shared<Emote>(Emote{
                .name = EmoteName{"[All]"},
                .images = ImageSet{image},
                .tooltip = Tooltip{"Message sent to every merged platform"},
                .homePage = Url{},
            });
        }();
        return allEmote;
    }

    // Load Twick (Twitch+Kick combined) favicon from local resources (cached as static)
    static auto twickEmote = []() -> EmotePtr {
        static const QPixmap pixmap(":/platforms/twick.png");
        if (pixmap.isNull())
        {
            qCWarning(chatterinoCommon) << "Failed to load Twick platform "
                                           "badge from :/platforms/twick.png";
            return nullptr;
        }
        // Scale to match other badges (16x16): 48px * (1/3) = 16px
        auto image = Image::fromResourcePixmap(pixmap, PLATFORM_BADGE_SCALE);
        return std::make_shared<Emote>(Emote{
            .name = EmoteName{"[Both]"},
            .images = ImageSet{image},
            .tooltip = Tooltip{"Message sent to both Twitch and Kick"},
            .homePage = Url{},
        });
    }();
    return twickEmote;
}

void MergedChannel::addSystemMessage(const QString &text)
{
    auto msg = makeSystemMessage(text);
    this->addMessage(msg, MessageContext::Original);
}

}  // namespace chatterino
