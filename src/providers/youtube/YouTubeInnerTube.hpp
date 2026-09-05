#pragma once

#include "providers/youtube/YouTubeMessage.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <vector>

class QNetworkAccessManager;

namespace chatterino {

/// Everything needed to keep polling one stream's live chat.
///
/// YouTube has no public streaming endpoint for live chat: the web player
/// long-polls an InnerTube RPC with an opaque continuation token, and every
/// response hands back the token for the next call. A session is that token
/// plus the client identity the RPC expects.
struct YouTubeChatSession {
    /// The web client's InnerTube key, lifted from the live_chat page.
    QString apiKey;
    /// e.g. "2.20260904.01.00". YouTube rejects a stale one.
    QString clientVersion;
    /// The token for the next poll, replaced on every response.
    QString continuation;

    QString videoId;
    /// The channel hosting the stream, for display and for /live re-resolution.
    QString channelId;
    QString channelName;
    /// Needed by the official Data API to send a message. Only present when the
    /// page exposed it.
    QString liveChatId;

    [[nodiscard]] bool valid() const
    {
        return !this->apiKey.isEmpty() && !this->continuation.isEmpty();
    }
};

/// One poll's worth of chat activity.
struct YouTubeChatPoll {
    bool ok{false};
    /// Set when the poll failed, or when the stream ended or chat closed.
    QString error;
    /// True when the failure is terminal (chat disabled, stream over) rather
    /// than something a retry would fix.
    bool fatal{false};

    std::vector<YouTubeMessage> messages;
    /// Messages retracted by a moderator since the last poll.
    std::vector<QString> deletedMessageIds;
    /// Authors banned since the last poll; all their messages should go.
    std::vector<QString> bannedAuthorIds;

    /// Token to use for the next poll. Empty means the chat is finished.
    QString nextContinuation;
    /// How long YouTube wants us to wait before polling again.
    int timeoutMs{5000};
};

/// Reader for YouTube live chat over the web client's InnerTube RPC.
///
/// This is the unofficial path, and it is the only one that works for a chat
/// client: the official Data API charges 5 quota units per poll against a
/// 10,000/day cap, which is roughly three hours of a single stream. Reading
/// here is anonymous, unmetered, and needs no Google account. Sending still
/// goes through the official API, see YouTubeApi.
class YouTubeInnerTube : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(YouTubeInnerTube)

public:
    explicit YouTubeInnerTube(QObject *parent = nullptr);
    ~YouTubeInnerTube() override;

    /// Turns whatever the user typed into a video id.
    ///
    /// Accepts a bare 11-character id, a watch/live/youtu.be URL, an @handle,
    /// or a /channel/UC... URL. Handles and channel URLs are resolved through
    /// the channel's /live page, so they follow the streamer from one broadcast
    /// to the next instead of pinning to one video.
    void resolveVideoId(const QString &input,
                        std::function<void(QString videoId, QString error)> cb);

    /// Loads the live_chat page for @a videoId and extracts a session.
    void openSession(
        const QString &videoId,
        std::function<void(YouTubeChatSession, QString error)> cb);

    /// Performs one long-poll. @a session supplies the continuation; the reply
    /// carries the next one.
    void poll(const YouTubeChatSession &session,
              std::function<void(YouTubeChatPoll)> cb);

    /// The user agent YouTube expects. The InnerTube endpoints return a stub
    /// page for anything that does not look like a browser.
    static QString userAgent();

private:
    void fetchLiveVideoIdForChannel(
        const QString &path,
        std::function<void(QString videoId, QString error)> cb);

    QNetworkAccessManager *network_;
};

}  // namespace chatterino
