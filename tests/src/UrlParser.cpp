#include "util/UrlParser.hpp"

#include "Test.hpp"

#include <QString>
#include <QStringList>

using namespace chatterino;

TEST(UrlParser, parsesPlainUsernames)
{
    const QStringList validUsernames = {
        "xqc",       "trainwreckstv", "amouranth", "destiny", "asmongold",
        "user_name", "user123",       "a",         "abc",
    };

    for (const auto &username : validUsernames)
    {
        auto result = UrlParser::parseKickChannel(username);
        ASSERT_TRUE(result.has_value()) << username;
        EXPECT_EQ(result->channelSlug, username.toLower()) << username;
    }
}

TEST(UrlParser, parsesKickUrls)
{
    struct UrlCase {
        QString input;
        QString expectedSlug;
    };

    const QList<UrlCase> cases = {
        {"kick.com/xqc", "xqc"},
        {"https://kick.com/xqc", "xqc"},
        {"http://kick.com/xqc", "xqc"},
        {"www.kick.com/xqc", "xqc"},
        {"https://www.kick.com/xqc", "xqc"},
        {"HTTPS://KICK.COM/XQC", "xqc"},
        {"kick.com/XqC", "xqc"},
        {"kick.com/trainwreckstv", "trainwreckstv"},
        {"https://kick.com/trainwreckstv/clips", "trainwreckstv"},
        {"kick.com/user_name", "user_name"},
        {"https://kick.com/user123", "user123"},
    };

    for (const auto &c : cases)
    {
        auto result = UrlParser::parseKickChannel(c.input);
        ASSERT_TRUE(result.has_value()) << c.input;
        EXPECT_EQ(result->channelSlug, c.expectedSlug) << c.input;
    }
}

TEST(UrlParser, rejectsInvalidInputs)
{
    const QStringList invalidInputs = {
        "",
        "   ",
        "kick.com/",
        "kick.com",
        "https://kick.com/",
        "https://kick.com",
        "https://twitch.tv/xqc",
        "a string with spaces",
        "user name",
        "@username",
        "#username",
        "https://",
        "://kick.com/xqc",
        // Too long username (over 25 chars)
        "abcdefghijklmnopqrstuvwxyz",
    };

    for (const auto &input : invalidInputs)
    {
        auto result = UrlParser::parseKickChannel(input);
        EXPECT_FALSE(result.has_value()) << input;
    }
}

TEST(UrlParser, handlesWhitespace)
{
    auto result1 = UrlParser::parseKickChannel("  xqc  ");
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->channelSlug, "xqc");

    auto result2 = UrlParser::parseKickChannel("  https://kick.com/xqc  ");
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->channelSlug, "xqc");
}

TEST(UrlParser, normalizesSlug)
{
    EXPECT_EQ(UrlParser::normalizeChannelSlug("XQC"), "xqc");
    EXPECT_EQ(UrlParser::normalizeChannelSlug("  xqc  "), "xqc");
    EXPECT_EQ(UrlParser::normalizeChannelSlug("TrainwrecksTv"),
              "trainwreckstv");
}

TEST(UrlParser, detectsKickUrls)
{
    EXPECT_TRUE(UrlParser::isKickUrl("kick.com/xqc"));
    EXPECT_TRUE(UrlParser::isKickUrl("https://kick.com/xqc"));
    EXPECT_TRUE(UrlParser::isKickUrl("http://kick.com/xqc"));
    EXPECT_TRUE(UrlParser::isKickUrl("www.kick.com/xqc"));
    EXPECT_TRUE(UrlParser::isKickUrl("KICK.COM/xqc"));

    EXPECT_FALSE(UrlParser::isKickUrl("xqc"));
    EXPECT_FALSE(UrlParser::isKickUrl("twitch.tv/xqc"));
    EXPECT_FALSE(UrlParser::isKickUrl("youtube.com/xqc"));
}
