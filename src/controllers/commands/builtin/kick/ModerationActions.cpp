#include "controllers/commands/builtin/kick/ModerationActions.hpp"

#include "controllers/commands/CommandContext.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChannel.hpp"
#include "util/Helpers.hpp"

#include <memory>

namespace {

using namespace chatterino;

/// Resolves @a username to a Kick user id, then runs @a action against the
/// channel being moderated. Kick has no bulk lookup, so this is one request per
/// command.
template <typename Action>
void withUser(KickChannel *channel, const QString &username,
              const QString &verb, Action action)
{
    auto api = channel->api();
    if (!api)
    {
        channel->addSystemMessage("Not connected to Kick.");
        return;
    }

    auto broadcasterUserId = channel->getBroadcasterUserId();
    if (broadcasterUserId == 0)
    {
        channel->addSystemMessage("Still resolving this channel, try again.");
        return;
    }

    api->resolveChannelInfo(
        username, [weak{channel->weak_from_this()}, api, username, verb, action,
                   broadcasterUserId](const KickApi::ChannelInfo &info) {
            auto shared = std::dynamic_pointer_cast<KickChannel>(weak.lock());
            if (!shared)
            {
                return;
            }

            if (!info.success || info.broadcasterUserId == 0)
            {
                shared->addSystemMessage("No Kick user named " + username);
                return;
            }

            action(api, broadcasterUserId, info.broadcasterUserId,
                   [weak, username, verb](const KickApiResult &result) {
                       auto shared =
                           std::dynamic_pointer_cast<KickChannel>(weak.lock());
                       if (!shared || result.success)
                       {
                           return;
                       }
                       shared->addSystemMessage("Failed to " + verb + " " +
                                                username + ": " +
                                                result.errorMessage);
                   });
        });
}

}  // namespace

namespace chatterino::commands {

bool kickBan(const CommandContext &ctx)
{
    if (ctx.kickChannel == nullptr)
    {
        return false;
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage("Usage: /ban <username> [reason]");
        return true;
    }

    auto reason = ctx.words.mid(2).join(' ');
    withUser(ctx.kickChannel, ctx.words[1], "ban",
             [reason](const std::shared_ptr<KickApi> &api, int broadcaster,
                      int user, auto callback) {
                 api->banUser(broadcaster, user, std::nullopt, reason,
                              std::move(callback));
             });
    return true;
}

bool kickTimeout(const CommandContext &ctx)
{
    if (ctx.kickChannel == nullptr)
    {
        return false;
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage(
            "Usage: /timeout <username> [duration] [reason]");
        return true;
    }

    // Kick counts timeouts in minutes, and defaults to 10 like Twitch's 600s
    int minutes = 10;
    qsizetype reasonStart = 2;
    if (ctx.words.size() >= 3)
    {
        auto seconds = parseDurationToSeconds(ctx.words[2], 1);
        if (seconds > 0)
        {
            minutes = std::max(1, static_cast<int>(seconds / 60));
            reasonStart = 3;
        }
    }

    auto reason = ctx.words.mid(reasonStart).join(' ');
    withUser(ctx.kickChannel, ctx.words[1], "time out",
             [minutes, reason](const std::shared_ptr<KickApi> &api,
                               int broadcaster, int user, auto callback) {
                 api->banUser(broadcaster, user, minutes, reason,
                              std::move(callback));
             });
    return true;
}

bool kickUnban(const CommandContext &ctx)
{
    if (ctx.kickChannel == nullptr)
    {
        return false;
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage("Usage: /unban <username>");
        return true;
    }

    withUser(ctx.kickChannel, ctx.words[1], "unban",
             [](const std::shared_ptr<KickApi> &api, int broadcaster, int user,
                auto callback) {
                 api->unbanUser(broadcaster, user, std::move(callback));
             });
    return true;
}

bool kickDeleteMessage(const CommandContext &ctx)
{
    if (ctx.kickChannel == nullptr)
    {
        return false;
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage("Usage: /delete <message id>");
        return true;
    }

    auto api = ctx.kickChannel->api();
    if (!api)
    {
        ctx.channel->addSystemMessage("Not connected to Kick.");
        return true;
    }

    auto messageId = ctx.words[1];
    api->deleteChatMessage(messageId, [weak{ctx.kickChannel->weak_from_this()},
                                       messageId](const KickApiResult &result) {
        auto shared = std::dynamic_pointer_cast<KickChannel>(weak.lock());
        if (!shared || result.success)
        {
            return;
        }
        shared->addSystemMessage("Failed to delete " + messageId + ": " +
                                 result.errorMessage);
    });
    return true;
}

}  // namespace chatterino::commands
