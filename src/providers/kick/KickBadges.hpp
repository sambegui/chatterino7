#pragma once

#include "messages/Emote.hpp"
#include "messages/MessageElement.hpp"

#include <utility>

namespace chatterino {

/// Kick has no public badge API, so the badge images ship with the client and
/// are looked up by the `type` field Kick sends on each chat message.
class KickBadges
{
public:
    /// Returns a null EmotePtr if the badge type is unknown to us.
    static std::pair<EmotePtr, MessageElementFlag> lookup(const QString &type);
};

}  // namespace chatterino
