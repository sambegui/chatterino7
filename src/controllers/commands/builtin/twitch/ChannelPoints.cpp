#include "controllers/commands/builtin/twitch/ChannelPoints.hpp"

#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/Helpers.hpp"

namespace {

using namespace chatterino;

void listRewards(const ChannelPtr &channel, qint64 balance,
                 const std::vector<gql::ChannelPointReward> &rewards)
{
    channel->addSystemMessage(QString("You have %1 channel points here.")
                                  .arg(localizeNumbers(balance)));

    if (rewards.empty())
    {
        channel->addSystemMessage("This channel has no custom rewards.");
        return;
    }

    for (const auto &reward : rewards)
    {
        channel->addSystemMessage(
            QString("%1 - %2 points%3")
                .arg(reward.title, localizeNumbers(reward.cost),
                     reward.needsInput ? " (asks for text)" : ""));
    }

    channel->addSystemMessage("Redeem one with /redeem <reward name> [text].");
}

}  // namespace

namespace chatterino::commands {

QString redeemChannelPoints(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "The /redeem command only works in Twitch channels.");
        return "";
    }

    auto unavailable = gql::unavailableReason();
    if (!unavailable.isEmpty())
    {
        ctx.channel->addSystemMessage(unavailable);
        return "";
    }

    auto channel = ctx.channel;
    auto channelId = ctx.twitchChannel->roomId();
    auto wanted = ctx.words.mid(1).join(' ').trimmed();

    gql::channelPointsContext(
        ctx.twitchChannel->getName(),
        [channel, channelId, wanted](
            qint64 balance,
            const std::vector<gql::ChannelPointReward> &rewards) {
            // no argument just reports what is on offer
            if (wanted.isEmpty())
            {
                listRewards(channel, balance, rewards);
                return;
            }

            // match the longest title that prefixes the argument, so the rest
            // of the line can be the reward's text input
            const gql::ChannelPointReward *match = nullptr;
            for (const auto &reward : rewards)
            {
                if (!wanted.startsWith(reward.title, Qt::CaseInsensitive))
                {
                    continue;
                }
                if (match == nullptr ||
                    reward.title.size() > match->title.size())
                {
                    match = &reward;
                }
            }

            if (match == nullptr)
            {
                channel->addSystemMessage("No reward here called \"" + wanted +
                                          "\". Run /redeem to list them.");
                return;
            }

            if (match->cost > balance)
            {
                channel->addSystemMessage(
                    QString("%1 costs %2 points and you have %3.")
                        .arg(match->title, localizeNumbers(match->cost),
                             localizeNumbers(balance)));
                return;
            }

            auto textInput = wanted.mid(match->title.size()).trimmed();
            if (match->needsInput && textInput.isEmpty())
            {
                channel->addSystemMessage(match->title +
                                          " needs some text: /redeem " +
                                          match->title + " <text>");
                return;
            }

            auto title = match->title;
            gql::redeemChannelPointReward(
                channelId, *match, textInput,
                [channel, title] {
                    channel->addSystemMessage("Redeemed " + title + ".");
                },
                [channel, title](const QString &error) {
                    channel->addSystemMessage("Could not redeem " + title +
                                              ": " + error);
                });
        },
        [channel](const QString &error) {
            channel->addSystemMessage("Could not read channel points: " +
                                      error);
        });

    return "";
}

}  // namespace chatterino::commands
