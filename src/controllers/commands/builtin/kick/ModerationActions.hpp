#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

/// Kick's own /ban, /timeout, /unban and /delete.
///
/// Each returns true when it handled the command, so the Twitch versions can
/// hand over when the split is on a Kick channel.
bool kickBan(const CommandContext &ctx);
bool kickTimeout(const CommandContext &ctx);
bool kickUnban(const CommandContext &ctx);
bool kickDeleteMessage(const CommandContext &ctx);

}  // namespace chatterino::commands
