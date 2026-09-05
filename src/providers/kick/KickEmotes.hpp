#pragma once

#include "common/Aliases.hpp"
#include "providers/kick/KickChannel.hpp"

#include <QString>

#include <functional>
#include <memory>
#include <unordered_map>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;
class EmoteMap;

/// Kick native emote management
/// Note: 7TV/BTTV/FFZ emotes are handled by existing infrastructure
/// and work automatically in Kick channels since those providers
/// use channel-agnostic emote sets.
class KickEmotes
{
public:
    KickEmotes();

    /// Get emote by name from Kick's native emote set
    /// @param emoteName The emote name to look up
    /// @return The emote if found, nullptr otherwise
    [[nodiscard]] EmotePtr getEmote(const QString &emoteName) const;

    /// Load global Kick emotes
    /// @param callback Called when loading completes
    void loadGlobalEmotes(std::function<void(bool success)> callback);

    /// Load channel-specific Kick emotes
    /// @param channelSlug The channel slug to load emotes for
    /// @param callback Called when loading completes
    void loadChannelEmotes(const QString &channelSlug,
                           std::function<void(bool success)> callback);

    /// Get all loaded global emotes (reference)
    [[nodiscard]] const EmoteMap &getGlobalEmotes() const;

    /// Get all loaded global emotes (shared_ptr for consistency with other providers)
    [[nodiscard]] std::shared_ptr<const EmoteMap> globalEmotes() const;

    /// Get channel emotes for a specific channel
    /// @param channelSlug The channel slug
    /// @return The channel's emote map, or nullptr if not loaded
    [[nodiscard]] std::shared_ptr<const EmoteMap> getChannelEmotes(
        const QString &channelSlug) const;

    /// Clear all cached emotes
    void clearCache();

private:
    /// Parse emote data from Kick API response
    void parseEmoteData(const QByteArray &data, EmoteMap &emoteMap);

    std::shared_ptr<EmoteMap> globalEmotes_;
    std::unordered_map<QString, std::shared_ptr<EmoteMap>> channelEmotes_;
};

}  // namespace chatterino
