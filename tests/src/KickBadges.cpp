#include "providers/kick/KickBadges.hpp"

#include "Test.hpp"

using namespace chatterino;

TEST(KickBadges, resolvesKnownBadges)
{
    auto [broadcaster, broadcasterFlag] = KickBadges::lookup("broadcaster");
    ASSERT_NE(broadcaster, nullptr);
    EXPECT_EQ(broadcaster->name.string, "Broadcaster");
    EXPECT_EQ(broadcasterFlag, MessageElementFlag::BadgeChannelAuthority);

    auto [subscriber, subscriberFlag] = KickBadges::lookup("subscriber");
    ASSERT_NE(subscriber, nullptr);
    EXPECT_EQ(subscriberFlag, MessageElementFlag::BadgeSubscription);

    auto [staff, staffFlag] = KickBadges::lookup("staff");
    ASSERT_NE(staff, nullptr);
    EXPECT_EQ(staffFlag, MessageElementFlag::BadgeGlobalAuthority);

    // underscored types must resolve verbatim
    auto [gifter, gifterFlag] = KickBadges::lookup("sub_gifter");
    ASSERT_NE(gifter, nullptr);
    EXPECT_EQ(gifter->name.string, "Sub Gifter");
    EXPECT_EQ(gifterFlag, MessageElementFlag::BadgeVanity);
}

TEST(KickBadges, returnsNullForUnknownBadge)
{
    EXPECT_EQ(KickBadges::lookup("not_a_badge").first, nullptr);
    EXPECT_EQ(KickBadges::lookup("").first, nullptr);
    // lookup is case sensitive, Kick always sends lowercase
    EXPECT_EQ(KickBadges::lookup("Broadcaster").first, nullptr);
}

TEST(KickBadges, cachesEmotes)
{
    auto first = KickBadges::lookup("vip").first;
    auto second = KickBadges::lookup("vip").first;
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first.get(), second.get());
}

TEST(KickBadges, loadsBundledImages)
{
    auto [vip, flag] = KickBadges::lookup("vip");
    ASSERT_NE(vip, nullptr);
    EXPECT_EQ(vip->images.getImage1()->url().string,
              ":/kick/badges/vip-18.webp");
    EXPECT_EQ(vip->images.getImage2()->url().string,
              ":/kick/badges/vip-36.webp");
}
