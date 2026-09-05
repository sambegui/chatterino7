#pragma once

#include "common/Channel.hpp"

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QString>

#include <chrono>
#include <memory>
#include <vector>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

/// Platform selection for sending messages in a merged channel
enum class PlatformSelection {
    Both,         // Send to all platforms
    TwitchOnly,   // Send only to Twitch
    KickOnly,     // Send only to Kick
    YouTubeOnly,  // Send only to YouTube
};

/// A virtual channel that combines messages from multiple source channels
/// (typically Twitch + Kick for the same streamer)
class MergedChannel : public Channel
{
public:
    /// Create a merged channel from multiple source channels
    /// @param name Display name for the merged channel
    /// @param sourceChannels The channels to merge messages from
    explicit MergedChannel(const QString &name,
                           std::vector<ChannelPtr> sourceChannels);
    ~MergedChannel() override;

    // Channel interface
    void sendMessage(const QString &message) override;
    bool isMod() const override;
    bool canSendMessage() const override;

    // Merged channel specific
    /// Get the display name showing all sources (e.g., "T:xqc + K:xqc")
    [[nodiscard]] const QString &getDisplayName() const override;

    /// Get all source channels
    [[nodiscard]] const std::vector<ChannelPtr> &getSourceChannels() const;

    /// Add a source channel to the merge
    void addSourceChannel(ChannelPtr channel);

    /// Remove a source channel from the merge
    void removeSourceChannel(const ChannelPtr &channel);

    /// Unmerge this channel back into its individual source channels
    /// Returns the list of source channels before clearing them
    /// After calling this, the merged channel should no longer be used
    [[nodiscard]] std::vector<ChannelPtr> unmerge();

    /// Get the current platform selection for sending
    [[nodiscard]] PlatformSelection getPlatformSelection() const;

    /// Set the platform selection for sending
    void setPlatformSelection(PlatformSelection selection);

    /// Check if a specific platform is available in this merged channel
    [[nodiscard]] bool hasPlatform(Channel::Type type) const;

    /// The source channel for a platform. Actions taken on a message (banning
    /// its author, running a command on it) have to reach the platform it came
    /// from, since the merge itself is not a channel anything can be done to.
    /// Null if this merge carries no channel of that type.
    [[nodiscard]] ChannelPtr sourceForPlatform(Channel::Type type) const;

    /// Signal emitted when platform selection changes
    pajlada::Signals::Signal<PlatformSelection> platformSelectionChanged;

    /// Signal emitted when source channels change
    pajlada::Signals::NoArgSignal sourceChannelsChanged;

private:
    /// Subscribe to a source channel's messages
    void subscribeToChannel(const ChannelPtr &channel);

    /// Handle message from a source channel
    void onSourceMessageReceived(MessagePtr message, Channel::Type sourceType);

    /// Get platform prefix for display
    static QString getPlatformPrefix(Channel::Type type);

    /// Human-readable platform name, for status and error lines.
    static QString platformDisplayName(Channel::Type type);

    /// Create a platform indicator badge emote using favicon
    static EmotePtr makePlatformBadge(Channel::Type type);

    /// Create a combined badge for a message the user sent to every platform
    /// in the merge. Twitch+Kick keeps its dedicated icon; any merge involving
    /// YouTube uses the generic three-platform one.
    static EmotePtr makeBothPlatformBadge(bool includesYouTube);

    /// Add a system message to the merged channel
    void addSystemMessage(const QString &text);

    /// Struct to track messages sent to both platforms for deduplication
    struct PendingSentMessage {
        QString messageText;
        std::chrono::steady_clock::time_point sentTime;
        int receivedFromPlatforms{0};
    };

    std::vector<ChannelPtr> sourceChannels_;
    PlatformSelection platformSelection_{PlatformSelection::Both};
    mutable QString cachedDisplayName_;
    std::vector<PendingSentMessage> pendingSentMessages_;

    pajlada::Signals::SignalHolder signalHolder_;

    /// Update the cached display name
    void updateCachedDisplayName() const;
};

}  // namespace chatterino
