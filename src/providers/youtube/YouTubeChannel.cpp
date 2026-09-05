#include "providers/youtube/YouTubeChannel.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/youtube/YouTubeAccount.hpp"
#include "providers/youtube/YouTubeApi.hpp"
#include "singletons/Settings.hpp"

#include <QRegularExpression>
#include <QTimer>

namespace {

using namespace chatterino;

/// YouTube asks for a 10 second gap between polls on a busy stream. Falling
/// much below that gets the continuation rejected, and there is nothing to gain
/// since the response already batches everything since the last call.
constexpr int MIN_POLL_INTERVAL_MS = 1000;
constexpr int MAX_POLL_INTERVAL_MS = 30000;

/// How long to wait before looking for a streamer's next broadcast once the
/// current one ends.
constexpr int REDISCOVER_INTERVAL_MS = 60000;

MessageColor colorForAuthor(const YouTubeAuthor &author)
{
    if (author.isOwner)
    {
        return MessageColor{QColor(255, 212, 0)};
    }
    if (author.isModerator)
    {
        return MessageColor{QColor(94, 132, 241)};
    }
    if (author.isMember)
    {
        return MessageColor{QColor(43, 166, 68)};
    }
    return MessageColor{QColor(170, 170, 170)};
}

}  // namespace

namespace chatterino {

YouTubeChannel::YouTubeChannel(const QString &target)
    : Channel(target, Channel::Type::YouTube)
    , target_(target)
    , innerTube_(std::make_unique<YouTubeInnerTube>())
    , pollTimer_(new QTimer)
    , rediscoverTimer_(new QTimer)
{
    static const QRegularExpression videoIdOnly{
        QStringLiteral("^[A-Za-z0-9_-]{11}$")};
    // A bare video id pins us to one broadcast; anything else names a channel
    // we can follow.
    this->targetIsChannel_ =
        !videoIdOnly.match(target.trimmed()).hasMatch() &&
        !target.contains(QStringLiteral("watch?v=")) &&
        !target.contains(QStringLiteral("youtu.be/")) &&
        !target.contains(QStringLiteral("/live/"));

    this->pollTimer_->setSingleShot(true);
    QObject::connect(this->pollTimer_, &QTimer::timeout, [this] {
        this->pollOnce();
    });

    this->rediscoverTimer_->setSingleShot(true);
    QObject::connect(this->rediscoverTimer_, &QTimer::timeout, [this] {
        this->resolveAndOpen();
    });
}

YouTubeChannel::~YouTubeChannel()
{
    this->pollTimer_->stop();
    this->rediscoverTimer_->stop();
    this->pollTimer_->deleteLater();
    this->rediscoverTimer_->deleteLater();
}

void YouTubeChannel::connect()
{
    if (this->connectionState_ == YouTubeConnectionState::Connected ||
        this->connectionState_ == YouTubeConnectionState::Resolving)
    {
        return;
    }

    this->resolveAndOpen();
}

void YouTubeChannel::disconnect()
{
    this->pollTimer_->stop();
    this->rediscoverTimer_->stop();
    this->setConnectionState(YouTubeConnectionState::Disconnected);
}

void YouTubeChannel::reconnect()
{
    this->pollTimer_->stop();
    this->rediscoverTimer_->stop();
    this->consecutiveFailures_ = 0;
    this->resolveAndOpen();
}

void YouTubeChannel::resolveAndOpen()
{
    this->setConnectionState(YouTubeConnectionState::Resolving);
    this->addSystemMessage(
        QStringLiteral("Looking for a live stream on %1...").arg(this->target_));

    auto self = std::weak_ptr<Channel>(this->shared_from_this());

    this->innerTube_->resolveVideoId(
        this->target_, [this, self](QString videoId, QString error) {
            if (self.expired())
            {
                return;
            }

            if (!error.isEmpty())
            {
                this->addSystemMessage(error);
                this->setConnectionState(YouTubeConnectionState::Failed);
                this->scheduleRediscovery();
                return;
            }

            this->videoId_ = videoId;

            this->innerTube_->openSession(
                videoId, [this, self](YouTubeChatSession session,
                                      QString sessionError) {
                    if (self.expired())
                    {
                        return;
                    }

                    if (!sessionError.isEmpty())
                    {
                        this->addSystemMessage(sessionError);
                        this->setConnectionState(YouTubeConnectionState::Failed);
                        this->scheduleRediscovery();
                        return;
                    }

                    this->session_ = session;
                    this->consecutiveFailures_ = 0;
                    this->setConnectionState(YouTubeConnectionState::Connected);
                    this->addSystemMessage(
                        QStringLiteral("Connected to YouTube live chat (%1)")
                            .arg(session.videoId));
                    this->liveStatusChanged.invoke();
                    this->pollOnce();
                });
        });
}

void YouTubeChannel::scheduleNextPoll(int timeoutMs)
{
    auto clamped = std::clamp(timeoutMs, MIN_POLL_INTERVAL_MS,
                              MAX_POLL_INTERVAL_MS);
    this->pollTimer_->start(clamped);
}

void YouTubeChannel::scheduleRediscovery()
{
    if (!this->targetIsChannel_)
    {
        // A pinned video id has nothing to rediscover.
        return;
    }
    this->rediscoverTimer_->start(REDISCOVER_INTERVAL_MS);
}

void YouTubeChannel::pollOnce()
{
    if (!this->session_.valid())
    {
        return;
    }

    auto self = std::weak_ptr<Channel>(this->shared_from_this());

    this->innerTube_->poll(this->session_, [this, self](YouTubeChatPoll poll) {
        if (self.expired())
        {
            return;
        }

        if (!poll.ok)
        {
            if (poll.fatal)
            {
                this->addSystemMessage(
                    this->targetIsChannel_
                        ? QStringLiteral(
                              "Live chat ended. Watching for the next stream...")
                        : QStringLiteral("Live chat ended."));
                this->setConnectionState(YouTubeConnectionState::Ended);
                this->liveStatusChanged.invoke();
                this->scheduleRediscovery();
                return;
            }

            this->consecutiveFailures_++;
            if (this->consecutiveFailures_ >= MAX_CONSECUTIVE_FAILURES)
            {
                this->addSystemMessage(
                    QStringLiteral("Lost the YouTube chat connection: %1")
                        .arg(poll.error));
                this->setConnectionState(YouTubeConnectionState::Failed);
                this->scheduleRediscovery();
                return;
            }

            this->setConnectionState(YouTubeConnectionState::Reconnecting);
            // Back off a little on each failure rather than hammering.
            this->scheduleNextPoll(2000 * this->consecutiveFailures_);
            return;
        }

        this->consecutiveFailures_ = 0;
        if (this->connectionState_ != YouTubeConnectionState::Connected)
        {
            this->setConnectionState(YouTubeConnectionState::Connected);
        }

        for (const auto &message : poll.messages)
        {
            this->handleMessage(message);
        }

        for (const auto &id : poll.deletedMessageIds)
        {
            this->disableMessage(id);
        }

        this->session_.continuation = poll.nextContinuation;
        this->scheduleNextPoll(poll.timeoutMs);
    });
}

void YouTubeChannel::handleMessage(const YouTubeMessage &message)
{
    MessageBuilder builder;

    builder.message().id = message.id;
    builder.message().serverReceivedTime = message.timestamp;
    builder.message().flags.set(MessageFlag::YouTube);

    builder.emplace<TimestampElement>(message.timestamp.time());

    if (message.author.isOwner)
    {
        if (auto badge = this->badgeFor(message.author.avatarUrl,
                                        QStringLiteral("Broadcaster")))
        {
            builder.emplace<BadgeElement>(
                badge, MessageElementFlag::BadgeChannelAuthority);
        }
    }

    if (message.author.isMember && !message.author.memberBadgeUrl.isEmpty())
    {
        auto tooltip = message.author.memberBadgeTooltip.isEmpty()
                           ? QStringLiteral("Member")
                           : message.author.memberBadgeTooltip;
        if (auto badge =
                this->badgeFor(message.author.memberBadgeUrl, tooltip))
        {
            builder.emplace<BadgeElement>(badge,
                                          MessageElementFlag::BadgeSubscription);
        }
    }

    QStringList prefixes;
    if (message.author.isModerator)
    {
        prefixes << QStringLiteral("MOD");
    }
    if (message.author.isVerified)
    {
        prefixes << QStringLiteral("VERIFIED");
    }
    if (!prefixes.isEmpty())
    {
        builder.emplace<TextElement>(
            QStringLiteral("[%1]").arg(prefixes.join('/')),
            MessageElementFlag::BadgeChannelAuthority,
            MessageColor{QColor(94, 132, 241)}, FontStyle::ChatMediumSmall);
    }

    builder
        .emplace<TextElement>(message.author.name + ':',
                              MessageElementFlag::Username,
                              colorForAuthor(message.author),
                              FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, message.author.name});

    // Superchats and membership events lead with what happened, since the body
    // on its own reads as an ordinary message.
    if (!message.purchaseAmount.isEmpty())
    {
        builder.emplace<TextElement>(
            message.purchaseAmount, MessageElementFlag::Text,
            MessageColor{message.bodyColor.isValid() ? message.bodyColor
                                                     : QColor(26, 115, 232)},
            FontStyle::ChatMediumBold);
    }
    if (!message.eventHeadline.isEmpty())
    {
        builder.emplace<TextElement>(message.eventHeadline,
                                     MessageElementFlag::Text,
                                     MessageColor{QColor(43, 166, 68)});
    }

    this->appendRuns(builder, message.runs);

    builder.message().loginName = message.author.channelId;
    builder.message().displayName = message.author.name;
    builder.message().searchText =
        message.author.name + ' ' + message.plainText();
    builder.message().messageText = message.plainText();

    if (message.kind != YouTubeItemKind::Text)
    {
        builder.message().flags.set(MessageFlag::Highlighted);
        if (message.bodyColor.isValid())
        {
            builder.message().highlightColor =
                std::make_shared<QColor>(message.bodyColor);
        }
    }

    this->addMessage(builder.release(), MessageContext::Original);
}

void YouTubeChannel::appendRuns(MessageBuilder &builder,
                                const std::vector<YouTubeRun> &runs)
{
    for (const auto &run : runs)
    {
        if (run.isEmoji())
        {
            if (auto emote = this->emoteFor(run))
            {
                builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);
                continue;
            }
            builder.emplace<TextElement>(run.emojiLabel,
                                         MessageElementFlag::Text,
                                         MessageColor::Text);
            continue;
        }

        // Split into words so third-party global emotes still resolve inside a
        // YouTube message, matching what the Kick channel does.
        for (const auto &word :
             run.text.split(' ', Qt::SkipEmptyParts))
        {
            EmotePtr thirdParty;
            if (auto *seventv = getApp()->getSeventvEmotes())
            {
                if (auto found = seventv->globalEmote(EmoteName{word}))
                {
                    thirdParty = *found;
                }
            }
            if (!thirdParty)
            {
                if (auto *bttv = getApp()->getBttvEmotes())
                {
                    if (auto found = bttv->emote(EmoteName{word}))
                    {
                        thirdParty = *found;
                    }
                }
            }
            if (!thirdParty)
            {
                if (auto *ffz = getApp()->getFfzEmotes())
                {
                    if (auto found = ffz->emote(EmoteName{word}))
                    {
                        thirdParty = *found;
                    }
                }
            }

            if (thirdParty)
            {
                builder.emplace<EmoteElement>(thirdParty,
                                              MessageElementFlag::Emote);
            }
            else
            {
                builder.emplace<TextElement>(word, MessageElementFlag::Text,
                                             MessageColor::Text);
            }
        }
    }
}

EmotePtr YouTubeChannel::emoteFor(const YouTubeRun &run)
{
    if (run.emojiUrl.isEmpty())
    {
        return nullptr;
    }

    auto it = this->emoteCache_.find(run.emojiUrl);
    if (it != this->emoteCache_.end())
    {
        return it.value();
    }

    auto name = EmoteName{run.emojiLabel};
    auto emote = std::make_shared<const Emote>(Emote{
        .name = name,
        .images = ImageSet{Image::fromUrl(Url{run.emojiUrl}, 1.0, {24, 24})},
        .tooltip = Tooltip{run.emojiLabel},
    });

    this->emoteCache_.insert(run.emojiUrl, emote);
    return emote;
}

EmotePtr YouTubeChannel::badgeFor(const QString &url, const QString &tooltip)
{
    if (url.isEmpty())
    {
        return nullptr;
    }

    auto it = this->badgeCache_.find(url);
    if (it != this->badgeCache_.end())
    {
        return it.value();
    }

    auto emote = std::make_shared<const Emote>(Emote{
        .name = EmoteName{tooltip},
        .images = ImageSet{Image::fromUrl(Url{url}, 1.0, {18, 18})},
        .tooltip = Tooltip{tooltip},
    });

    this->badgeCache_.insert(url, emote);
    return emote;
}

void YouTubeChannel::sendMessage(const QString &message)
{
    if (message.trimmed().isEmpty())
    {
        return;
    }

    if (!this->api_ || !this->account_ || !this->account_->isAuthenticated())
    {
        this->addSystemMessage(
            QStringLiteral("Sign in to YouTube in Settings > Platforms to send "
                           "messages. Reading chat needs no account."));
        return;
    }

    if (this->videoId_.isEmpty())
    {
        this->addSystemMessage(QStringLiteral("Not connected to a stream yet."));
        return;
    }

    auto self = std::weak_ptr<Channel>(this->shared_from_this());
    auto text = message;

    auto send = [this, self, text](const QString &liveChatId) {
        this->api_->sendMessage(
            liveChatId, text, [this, self](bool ok, QString error) {
                if (self.expired() || ok)
                {
                    return;
                }
                this->addSystemMessage(
                    QStringLiteral("Could not send to YouTube: %1").arg(error));
            });
    };

    if (!this->liveChatId_.isEmpty())
    {
        send(this->liveChatId_);
        return;
    }

    // The live chat id is only needed for sending, so it is resolved on demand
    // rather than on every join.
    this->api_->resolveLiveChatId(
        this->videoId_, [this, self, send](QString liveChatId, QString error) {
            if (self.expired())
            {
                return;
            }
            if (liveChatId.isEmpty())
            {
                this->addSystemMessage(
                    QStringLiteral("Could not find the live chat to send to: %1")
                        .arg(error));
                return;
            }
            this->liveChatId_ = liveChatId;
            send(liveChatId);
        });
}

bool YouTubeChannel::isMod() const
{
    return false;
}

bool YouTubeChannel::isBroadcaster() const
{
    return false;
}

bool YouTubeChannel::hasModRights() const
{
    return false;
}

bool YouTubeChannel::canSendMessage() const
{
    return this->account_ && this->account_->isAuthenticated() &&
           this->connectionState_ == YouTubeConnectionState::Connected;
}

bool YouTubeChannel::isLive() const
{
    return this->connectionState_ == YouTubeConnectionState::Connected;
}

void YouTubeChannel::setConnectionState(YouTubeConnectionState state)
{
    if (this->connectionState_ == state)
    {
        return;
    }
    this->connectionState_ = state;
    this->connectionStateChanged.invoke(state);
}

YouTubeConnectionState YouTubeChannel::getConnectionState() const
{
    return this->connectionState_;
}

const QString &YouTubeChannel::getTarget() const
{
    return this->target_;
}

const QString &YouTubeChannel::getVideoId() const
{
    return this->videoId_;
}

void YouTubeChannel::setAccount(std::shared_ptr<YouTubeAccount> account)
{
    this->account_ = std::move(account);
}

void YouTubeChannel::setApi(std::shared_ptr<YouTubeApi> api)
{
    this->api_ = std::move(api);
}

void YouTubeChannel::addSystemMessage(const QString &text)
{
    MessageBuilder builder;
    builder.emplace<TimestampElement>();
    builder.emplace<TextElement>(text, MessageElementFlag::Text,
                                 MessageColor::System);
    builder.message().flags.set(MessageFlag::System);
    builder.message().flags.set(MessageFlag::DoNotTriggerNotification);
    builder.message().flags.set(MessageFlag::YouTube);
    this->addMessage(builder.release(), MessageContext::Original);
}

}  // namespace chatterino
