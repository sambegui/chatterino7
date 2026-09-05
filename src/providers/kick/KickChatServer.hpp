#pragma once

#include "util/QStringHash.hpp"

#include <QString>

#include <memory>
#include <unordered_map>

namespace chatterino {

class KickChannel;
class KickWebSocket;
struct KickMessage;

/// Owns the single connection to Kick's chat and the channels reading from it.
///
/// Kick multiplexes every chatroom over one Pusher socket, so opening ten Kick
/// splits should still be one connection. This also keeps one KickChannel per
/// slug, so the same channel in two splits is the same object.
class KickChatServer
{
public:
    KickChatServer();
    ~KickChatServer();

    KickChatServer(const KickChatServer &) = delete;
    KickChatServer(KickChatServer &&) = delete;
    KickChatServer &operator=(const KickChatServer &) = delete;
    KickChatServer &operator=(KickChatServer &&) = delete;

    /// Returns the channel for @a slug, creating and connecting one if this is
    /// the first split to ask for it.
    std::shared_ptr<KickChannel> getOrCreate(const QString &slug);

    /// Returns an already-open channel, or nullptr.
    [[nodiscard]] std::shared_ptr<KickChannel> findBySlug(
        const QString &slug) const;

    /// Subscribes a resolved channel's chatroom on the shared socket. Called by
    /// KickChannel once it knows its chatroom id.
    void subscribeChatroom(int chatroomId,
                           const std::shared_ptr<KickChannel> &channel);

    /// Opens the socket if it is not up yet.
    void ensureConnected();

    /// Drops @a channel's subscription and its registry entries.
    void leave(KickChannel *channel);

    [[nodiscard]] bool isConnected() const;

private:
    void onMessage(const KickMessage &message);
    void onConnectionStateChanged(bool connected);
    void onError(const QString &error);

    /// Drops entries whose channel has been destroyed.
    void pruneExpired();

    std::unique_ptr<KickWebSocket> socket_;
    std::unordered_map<QString, std::weak_ptr<KickChannel>> bySlug_;
    std::unordered_map<int, std::weak_ptr<KickChannel>> byChatroom_;
};

}  // namespace chatterino
