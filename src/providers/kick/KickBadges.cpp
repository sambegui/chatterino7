#include "providers/kick/KickBadges.hpp"

#include "debug/AssertInGuiThread.hpp"
#include "messages/Image.hpp"

#include <array>

namespace {

using namespace chatterino;

struct BadgeDefinition {
    const char *type;
    const char *friendlyName;
    MessageElementFlag flag;
};

constexpr std::array BADGES{
    BadgeDefinition{"bot", "Bot", MessageElementFlag::BadgeVanity},
    BadgeDefinition{"broadcaster", "Broadcaster",
                    MessageElementFlag::BadgeChannelAuthority},
    BadgeDefinition{"founder", "Founder",
                    MessageElementFlag::BadgeSubscription},
    BadgeDefinition{"moderator", "Moderator",
                    MessageElementFlag::BadgeChannelAuthority},
    BadgeDefinition{"og", "OG", MessageElementFlag::BadgeVanity},
    BadgeDefinition{"sidekick", "Sidekick", MessageElementFlag::BadgeVanity},
    BadgeDefinition{"staff", "Staff", MessageElementFlag::BadgeGlobalAuthority},
    BadgeDefinition{"sub_gifter", "Sub Gifter",
                    MessageElementFlag::BadgeVanity},
    BadgeDefinition{"subscriber", "Subscriber",
                    MessageElementFlag::BadgeSubscription},
    BadgeDefinition{"trainwreckstv", "TrainwrecksTV",
                    MessageElementFlag::BadgeVanity},
    BadgeDefinition{"verified", "Verified", MessageElementFlag::BadgeVanity},
    BadgeDefinition{"vip", "VIP", MessageElementFlag::BadgeChannelAuthority},
};

std::array<EmotePtr, BADGES.size()> emoteCache;

}  // namespace

namespace chatterino {

std::pair<EmotePtr, MessageElementFlag> KickBadges::lookup(const QString &type)
{
    assertInGuiThread();

    for (size_t i = 0; i < BADGES.size(); i++)
    {
        if (type != QLatin1String(BADGES[i].type))
        {
            continue;
        }

        auto &cached = emoteCache[i];
        if (!cached)
        {
            auto name = QString::fromLatin1(BADGES[i].friendlyName);
            auto path = QStringLiteral(":/kick/badges/") +
                        QLatin1String(BADGES[i].type);

            cached = std::make_shared<const Emote>(Emote{
                .name = EmoteName{name},
                .images =
                    ImageSet{
                        Image::fromUrl(Url{path + "-18.webp"}, 1.0, {18, 18}),
                        Image::fromUrl(Url{path + "-36.webp"}, 0.5, {36, 36}),
                    },
                .tooltip = Tooltip{name},
            });
        }

        return {cached, BADGES[i].flag};
    }

    return {nullptr, {}};
}

}  // namespace chatterino
