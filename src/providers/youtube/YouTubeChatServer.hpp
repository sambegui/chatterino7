#pragma once

#include "util/QStringHash.hpp"

#include <QString>

#include <memory>
#include <unordered_map>

namespace chatterino {

class YouTubeChannel;
class YouTubeAccount;
class YouTubeApi;

/// Keeps one YouTubeChannel per target so the same stream in two splits shares
/// a single poll loop.
///
/// Unlike Kick there is no multiplexed socket to share: YouTube gives each
/// stream its own continuation, so this is purely a deduplicating registry.
/// That matters more here than it looks, because every duplicate split would
/// otherwise mean another HTTP long-poll every few seconds.
class YouTubeChatServer
{
public:
    YouTubeChatServer();
    ~YouTubeChatServer();

    YouTubeChatServer(const YouTubeChatServer &) = delete;
    YouTubeChatServer(YouTubeChatServer &&) = delete;
    YouTubeChatServer &operator=(const YouTubeChatServer &) = delete;
    YouTubeChatServer &operator=(YouTubeChatServer &&) = delete;

    /// Returns the channel for @a target, creating one if this is the first
    /// split to ask. @a target is a handle, channel URL, or video id.
    std::shared_ptr<YouTubeChannel> getOrCreate(const QString &target);

    /// Returns an already-open channel, or nullptr.
    [[nodiscard]] std::shared_ptr<YouTubeChannel> find(
        const QString &target) const;

    /// The single sign-in used for sending. Created and loaded from settings on
    /// first use; reading chat never touches it.
    [[nodiscard]] std::shared_ptr<YouTubeAccount> account();
    [[nodiscard]] std::shared_ptr<YouTubeApi> api();

    /// True when a refresh token is stored, so the UI can show who is signed in.
    [[nodiscard]] bool isSignedIn();

    /// Drops the stored tokens.
    void signOut();

private:
    void pruneExpired();

    std::unordered_map<QString, std::weak_ptr<YouTubeChannel>> channels_;
    std::shared_ptr<YouTubeAccount> account_;
    std::shared_ptr<YouTubeApi> api_;
};

}  // namespace chatterino
