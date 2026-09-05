#include "providers/kick/KickChannel.hpp"

#include "providers/kick/KickAccount.hpp"
#include "Test.hpp"

#include <QDateTime>
#include <QString>

using namespace chatterino;

TEST(KickChannel, createsWithCorrectType)
{
    KickChannel channel("testchannel");

    EXPECT_EQ(channel.getName(), "testchannel");
    EXPECT_EQ(channel.getType(), Channel::Type::Kick);
}

TEST(KickChannel, initialStateIsDisconnected)
{
    KickChannel channel("testchannel");

    EXPECT_EQ(channel.getConnectionState(), KickConnectionState::Disconnected);
}

TEST(KickChannel, channelSlugIsPreserved)
{
    KickChannel channel("xqc");
    EXPECT_EQ(channel.getChannelSlug(), "xqc");

    KickChannel channel2("trainwreckstv");
    EXPECT_EQ(channel2.getChannelSlug(), "trainwreckstv");
}

TEST(KickChannel, notAuthenticatedByDefault)
{
    KickChannel channel("testchannel");

    EXPECT_FALSE(channel.isAuthenticated());
    EXPECT_FALSE(channel.canSendMessage());
}

TEST(KickChannel, canSetAuthenticated)
{
    KickChannel channel("testchannel");

    channel.setAuthenticated(true);
    EXPECT_TRUE(channel.isAuthenticated());

    channel.setAuthenticated(false);
    EXPECT_FALSE(channel.isAuthenticated());
}

TEST(KickChannel, chatroomIdDefaultsToZero)
{
    KickChannel channel("testchannel");

    EXPECT_EQ(channel.getChatroomId(), 0);
    EXPECT_EQ(channel.getBroadcasterUserId(), 0);
}

TEST(KickChannel, isNotModByDefault)
{
    KickChannel channel("testchannel");

    EXPECT_FALSE(channel.isMod());
}

TEST(KickConnectionState, enumValuesExist)
{
    // Verify all connection states are defined
    auto disconnected = KickConnectionState::Disconnected;
    auto connecting = KickConnectionState::Connecting;
    auto connected = KickConnectionState::Connected;
    auto reconnecting = KickConnectionState::Reconnecting;
    auto failed = KickConnectionState::Failed;

    EXPECT_NE(static_cast<int>(disconnected), static_cast<int>(connecting));
    EXPECT_NE(static_cast<int>(connected), static_cast<int>(reconnecting));
    EXPECT_NE(static_cast<int>(reconnecting), static_cast<int>(failed));
}

TEST(KickChannel, roomModesReportWhetherAnyAreOn)
{
    KickApi::RoomModes none;
    EXPECT_FALSE(none.any());

    KickApi::RoomModes subs;
    subs.subscribersOnly = true;
    EXPECT_TRUE(subs.any());

    KickApi::RoomModes slow;
    slow.slowModeInterval = 5;
    EXPECT_TRUE(slow.any());

    KickApi::RoomModes followers;
    followers.followersOnlyDuration = 5760;
    EXPECT_TRUE(followers.any());

    // an interval of 0 means the mode is off, not "no wait"
    KickApi::RoomModes offWithInterval;
    offWithInterval.slowModeInterval = 0;
    EXPECT_FALSE(offWithInterval.any());
}

TEST(KickChannel, ownChannelCountsAsBroadcaster)
{
    auto account = std::make_shared<KickAccount>(
        "menzocs", "token", "refresh",
        QDateTime::currentDateTime().addSecs(3600));

    KickChannel own("menzocs");
    own.setAccount(account);
    EXPECT_TRUE(own.isBroadcaster());
    EXPECT_TRUE(own.hasModRights());

    // casing must not matter, the slug is lowercase but the login may not be
    KickChannel cased("MenzoCS");
    cased.setAccount(account);
    EXPECT_TRUE(cased.isBroadcaster());

    // someone else's channel is not ours to moderate without a role
    KickChannel other("xqc");
    other.setAccount(account);
    EXPECT_FALSE(other.isBroadcaster());
    EXPECT_FALSE(other.hasModRights());
}

TEST(KickChannel, anonymousUserIsNeverBroadcaster)
{
    KickChannel channel("menzocs");
    EXPECT_FALSE(channel.isBroadcaster());
    EXPECT_FALSE(channel.hasModRights());
}
