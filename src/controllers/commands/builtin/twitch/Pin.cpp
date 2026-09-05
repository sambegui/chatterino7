#include "controllers/commands/builtin/twitch/Pin.hpp"

#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/Helpers.hpp"

namespace {

using namespace chatterino;

constexpr const char *PIN_HASH =
    "214191369c21f1ad67ac074795d53832329c70e4088c979040c9f86334a7d736";
constexpr const char *UNPIN_HASH =
    "86409b9c86510bdc9f2c6d8e58fdc4041963c001de53577160ab649e03334511";

}  // namespace

namespace chatterino::commands {

QString pinMessage(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "The /pin command only works in Twitch channels.");
        return "";
    }

    auto unavailable = gql::unavailableReason();
    if (!unavailable.isEmpty())
    {
        ctx.channel->addSystemMessage(unavailable);
        return "";
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage(
            "Usage: /pin <message id> [duration]. Duration defaults to "
            "Twitch's own, and accepts values like 60s or 5m.");
        return "";
    }

    int durationSeconds = 0;
    if (ctx.words.size() >= 3)
    {
        auto parsed = parseDurationToSeconds(ctx.words[2], 1);
        if (parsed > 0)
        {
            durationSeconds = static_cast<int>(parsed);
        }
    }

    QJsonObject input{
        {"channelID", ctx.twitchChannel->roomId()},
        {"messageID", ctx.words[1]},
        {"type", "MOD"},
    };
    if (durationSeconds > 0)
    {
        input["durationSeconds"] = durationSeconds;
    }

    auto channel = ctx.channel;
    gql::persistedQuery(
        "PinChatMessage", PIN_HASH, {{"input", input}},
        [channel](const QJsonObject &) {
            // the pinned-chat event will bring the banner up on its own
            channel->addSystemMessage("Pinned the message.");
        },
        [channel](const QString &error) {
            channel->addSystemMessage("Could not pin the message: " + error);
        });

    return "";
}

QString unpinMessage(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "The /unpin command only works in Twitch channels.");
        return "";
    }

    auto unavailable = gql::unavailableReason();
    if (!unavailable.isEmpty())
    {
        ctx.channel->addSystemMessage(unavailable);
        return "";
    }

    auto pin = ctx.twitchChannel->pinnedMessage();
    if (!pin)
    {
        ctx.channel->addSystemMessage("Nothing is pinned in this channel.");
        return "";
    }

    QJsonObject input{
        {"id", pin->pinId},
        {"reason", "UNPIN"},
    };

    auto channel = ctx.channel;
    gql::persistedQuery(
        "unpinChatMessage", UNPIN_HASH, {{"input", input}},
        [channel](const QJsonObject &) {
            channel->addSystemMessage("Removed the pinned message.");
        },
        [channel](const QString &error) {
            channel->addSystemMessage("Could not unpin the message: " + error);
        });

    return "";
}

}  // namespace chatterino::commands
