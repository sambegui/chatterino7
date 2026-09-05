#pragma once

#include "common/Channel.hpp"
#include "providers/youtube/YouTubeInnerTube.hpp"
#include "providers/youtube/YouTubeMessage.hpp"

#include <pajlada/signals/signal.hpp>
#include <QHash>
#include <QString>

#include <memory>

class QTimer;

namespace chatterino {

class MessageBuilder;
class YouTubeApi;
class YouTubeAccount;
struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

enum class YouTubeConnectionState {
    Disconnected,
    Resolving,
    Connected,
    Reconnecting,
    Ended,
    Failed,
};

/// A YouTube live chat, read over the InnerTube long-poll.
///
/// The channel is addressed by whatever the user typed (a handle, a channel
/// URL, or a video id). When it is a handle the channel re-resolves through the
/// /live page, so it follows the streamer across broadcasts instead of dying
/// with one video.
class YouTubeChannel : public Channel
{
public:
    explicit YouTubeChannel(const QString &target);
    ~YouTubeChannel() override;

    // Channel interface
    void sendMessage(const QString &message) override;
    bool isMod() const override;
    bool isBroadcaster() const override;
    bool hasModRights() const override;
    bool canSendMessage() const override;
    bool isLive() const override;
    void reconnect() override;

    /// Resolves the target and starts polling.
    void connect();
    void disconnect();

    [[nodiscard]] YouTubeConnectionState getConnectionState() const;
    /// What the user typed: a handle, URL, or video id.
    [[nodiscard]] const QString &getTarget() const;
    [[nodiscard]] const QString &getVideoId() const;

    void setAccount(std::shared_ptr<YouTubeAccount> account);
    void setApi(std::shared_ptr<YouTubeApi> api);

    pajlada::Signals::Signal<YouTubeConnectionState> connectionStateChanged;
    pajlada::Signals::NoArgSignal liveStatusChanged;

private:
    void setConnectionState(YouTubeConnectionState state);

    void resolveAndOpen();
    void scheduleNextPoll(int timeoutMs);
    void pollOnce();

    /// Re-resolves the target after a stream ends, so a handle picks up the
    /// streamer's next broadcast without the user rejoining.
    void scheduleRediscovery();

    void handleMessage(const YouTubeMessage &message);
    void appendRuns(MessageBuilder &builder,
                    const std::vector<YouTubeRun> &runs);

    /// Emotes are per-URL: YouTube ships every custom emoji as an image with no
    /// stable id we could share across channels.
    EmotePtr emoteFor(const YouTubeRun &run);
    EmotePtr badgeFor(const QString &url, const QString &tooltip);

    void addSystemMessage(const QString &text);

    QString target_;
    QString videoId_;
    YouTubeChatSession session_;
    YouTubeConnectionState connectionState_{
        YouTubeConnectionState::Disconnected};

    /// True when the target was a handle or channel URL, which means it can be
    /// re-resolved to a later broadcast.
    bool targetIsChannel_{false};

    std::unique_ptr<YouTubeInnerTube> innerTube_;
    QTimer *pollTimer_;
    QTimer *rediscoverTimer_;

    std::shared_ptr<YouTubeApi> api_;
    std::shared_ptr<YouTubeAccount> account_;
    /// Resolved lazily on the first send, since reading never needs it.
    QString liveChatId_;

    QHash<QString, EmotePtr> emoteCache_;
    QHash<QString, EmotePtr> badgeCache_;

    int consecutiveFailures_{0};
    static constexpr int MAX_CONSECUTIVE_FAILURES = 5;
};

}  // namespace chatterino
