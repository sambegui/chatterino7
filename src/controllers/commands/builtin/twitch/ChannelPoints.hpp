#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

/// Lists the channel's rewards and your balance, or redeems one by name.
/// Needs Twitch's private API; the public one has no way for a viewer to
/// redeem anything.
QString redeemChannelPoints(const CommandContext &ctx);

}  // namespace chatterino::commands
