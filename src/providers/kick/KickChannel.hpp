#pragma once

#include "common/Aliases.hpp"
#include "common/Channel.hpp"
#include "messages/Emote.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickMessage.hpp"

#include <pajlada/signals/signal.hpp>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>
#include <shared_mutex>

namespace chatterino {

class EmoteMap;
class MessageBuilder;
class KickWebSocket;
class KickAccount;
class KickApi;

/// Connection state for a Kick channel
enum class KickConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed
};

/// Represents a Kick.com chat channel with real-time message streaming
class KickChannel : public Channel
{
public:
    explicit KickChannel(const QString &channelSlug);
    ~KickChannel() override;

    // Channel interface
    void sendMessage(const QString &message) override;
    bool isMod() const override;
    bool isVip() const;
    bool isBroadcaster() const override;
    bool hasModRights() const override;

    /// The chat restrictions this channel currently has switched on.
    [[nodiscard]] const KickApi::RoomModes &roomModes() const;

    /// Fires when the chat restrictions change.
    pajlada::Signals::NoArgSignal roomModesChanged;
    bool canSendMessage() const override;
    bool isLive() const override;

    // Kick-specific methods
    void connect();
    void disconnect();

    /// Called by KickChatServer as the shared connection changes state.
    void onServerConnected();
    void onServerDisconnected();
    void onServerError();

    /// Called by KickChatServer when a message arrives for this chatroom.
    void onMessageReceived(const KickMessage &message);

    void reconnect() override;
    [[nodiscard]] KickConnectionState getConnectionState() const;
    [[nodiscard]] QString getChannelSlug() const;
    [[nodiscard]] int getChatroomId() const;
    [[nodiscard]] int getBroadcasterUserId() const;

    // Authentication
    void setAuthenticated(bool authenticated);
    [[nodiscard]] bool isAuthenticated() const;

    // API access
    void setApi(std::shared_ptr<KickApi> api);
    [[nodiscard]] std::shared_ptr<KickApi> api() const;
    void setAccount(std::shared_ptr<KickAccount> account);

    // Recent messages (stub - Kick history API not available)
    void fetchRecentMessages();

    /// Refresh 7TV channel emotes (uses Kick-specific 7TV endpoint)
    /// @param manualRefresh If true, shows system messages on completion
    void refreshSevenTVChannelEmotes(bool manualRefresh = false);

    /// Refresh native Kick channel emotes
    /// @param manualRefresh If true, shows system messages on completion
    void refreshKickChannelEmotes(bool manualRefresh = false);

    /// Get 7TV emotes for this channel
    [[nodiscard]] std::shared_ptr<const EmoteMap> getSeventvEmotes() const;

    /// Get native Kick emotes seen in this channel (accumulated from messages)
    [[nodiscard]] std::shared_ptr<const EmoteMap> getNativeKickEmotes() const;

    /// Get available Kick emotes that the user can actually use
    /// This is filtered based on the user's subscription status
    [[nodiscard]] std::shared_ptr<const EmoteMap> getAvailableKickEmotes()
        const;

    /// Fetch available emotes from Kick API and filter by user's access
    /// @param callback Called when emotes are loaded (success, count)
    void fetchAvailableEmotes(
        std::function<void(bool success, int count)> callback = nullptr);

    /// Check if user has subscriber access to this channel
    [[nodiscard]] bool hasSubscriberAccess() const;

    /// Signal emitted when available emotes are updated
    pajlada::Signals::NoArgSignal availableEmotesUpdated;

    /// Signal emitted when connection state changes
    pajlada::Signals::Signal<KickConnectionState> connectionStateChanged;

    /// Signal emitted when stream status changes (live/offline)
    pajlada::Signals::NoArgSignal liveStatusChanged;

private:
    /// Update connection state and emit signal
    void setConnectionState(KickConnectionState state);

    /// Resolve channel slug to chatroom ID and subscribe
    void resolveAndSubscribe();

    /// Handle WebSocket connection error
    void handleConnectionError();

    /// Schedule reconnection with exponential backoff
    void scheduleReconnect();

    /// Parse message content and emotes into message elements
    /// @param builder The MessageBuilder to add elements to
    /// @param content The raw message content
    /// @param emotes The emote list with positions
    void parseMessageContent(MessageBuilder &builder, const QString &content,
                             const std::vector<KickEmote> &emotes);

    /// Find a third-party emote (7TV, BTTV, FFZ) by name
    /// @param word The emote name to look up
    /// @return The emote if found, nullopt otherwise
    std::optional<EmotePtr> findThirdPartyEmote(const QString &word) const;

    /// Add text or emote to message builder, checking for third-party emotes
    /// @param builder The MessageBuilder to add elements to
    /// @param text The text to add (may contain emote names)
    void addTextOrEmote(MessageBuilder &builder, const QString &text) const;

    /// Convert native Kick emote names to [emote:ID:NAME] format for sending
    /// @param message The message to process
    /// @return The message with emote names converted to Kick format
    QString convertEmotesForSending(const QString &message) const;

    QString channelSlug_;
    int chatroomId_{0};         // Used for WebSocket subscription
    int broadcasterUserId_{0};  // Used for REST API (sending messages)
    KickConnectionState connectionState_;
    bool isAuthenticated_{false};
    KickApi::RoomModes roomModes_;
    QString userRole_;
    bool isLive_{false};
    QString streamTitle_;
    int viewerCount_{0};

    std::shared_ptr<KickApi> api_;
    std::shared_ptr<KickAccount> account_;

    // 7TV emotes for this Kick channel
    std::shared_ptr<const EmoteMap> seventvEmotes_;

    // Native Kick emotes seen in this channel (accumulated from messages)
    mutable std::shared_mutex nativeEmotesMutex_;
    std::shared_ptr<EmoteMap> nativeKickEmotes_;

    // Available Kick emotes the user can send (filtered by subscription status)
    mutable std::shared_mutex availableEmotesMutex_;
    std::shared_ptr<EmoteMap> availableKickEmotes_;
    bool hasSubscriberAccess_{false};
    bool emotesLoaded_{false};

    // Reconnection state
    int reconnectAttempts_{0};
    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;

    /// Load 7TV cosmetics (paints, badges) for a Kick user
    /// @param kickUserId The Kick user ID
    /// @param userName The username to assign cosmetics to
    void loadUserSevenTVCosmetics(int kickUserId, const QString &userName);

    /// Track users whose cosmetics we've already fetched
    mutable std::shared_mutex loadedUsersMutex_;
    QSet<int> usersWithLoadedCosmetics_;
};

}  // namespace chatterino
