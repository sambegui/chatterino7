#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

/// Pins a message in the channel. Needs Twitch's private API, so it reports
/// what is missing rather than failing silently when that is switched off.
QString pinMessage(const CommandContext &ctx);

/// Removes the channel's current pin.
QString unpinMessage(const CommandContext &ctx);

}  // namespace chatterino::commands
