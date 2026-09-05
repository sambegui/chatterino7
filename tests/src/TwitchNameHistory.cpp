#include "providers/twitch/TwitchNameHistory.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

QJsonArray parseArray(const char *json)
{
    return QJsonDocument::fromJson(json).array();
}

}  // namespace

TEST(TwitchNameHistory, ordersNewestFirstAndMarksCurrentLogin)
{
    // as returned for xqc: oldest login first
    auto root = parseArray(R"([
        {"user_login":"xqcow",
         "first_timestamp":"2016-10-20T20:07:43Z",
         "last_timestamp":"2022-05-19T07:49:53.011Z"},
        {"user_login":"xqc",
         "first_timestamp":"2018-08-16T00:47:26Z",
         "last_timestamp":"2026-09-02T22:13:48.165Z"}
    ])");

    auto history = parseTwitchNameHistory(root, "71092938", "xqc");

    EXPECT_EQ(history.userId, "71092938");
    EXPECT_EQ(history.currentLogin, "xqc");
    ASSERT_EQ(history.entries.size(), 2);

    // the login held now comes first and has no end date
    EXPECT_EQ(history.entries[0].login, "xqc");
    EXPECT_EQ(history.entries[0].lastSeen, "Present");

    EXPECT_EQ(history.entries[1].login, "xqcow");
    EXPECT_NE(history.entries[1].lastSeen, "Present");
    // the oldest entry's start predates the logs service
    EXPECT_EQ(history.entries[1].firstSeen, "Unknown");
}

TEST(TwitchNameHistory, picksCurrentLoginByNewestLastSeen)
{
    // a user who reverted to an earlier login: order in the array does not
    // decide which login is current
    auto root = parseArray(R"([
        {"user_login":"first",
         "first_timestamp":"2015-01-01T00:00:00Z",
         "last_timestamp":"2030-01-01T00:00:00Z"},
        {"user_login":"second",
         "first_timestamp":"2016-01-01T00:00:00Z",
         "last_timestamp":"2017-01-01T00:00:00Z"}
    ])");

    auto history = parseTwitchNameHistory(root, "1", "first");

    EXPECT_EQ(history.currentLogin, "first");
    ASSERT_EQ(history.entries.size(), 2);
    EXPECT_EQ(history.entries[1].login, "first");
    EXPECT_EQ(history.entries[1].lastSeen, "Present");
    EXPECT_EQ(history.entries[0].login, "second");
}

TEST(TwitchNameHistory, normalizesAndSkipsBlankLogins)
{
    auto root = parseArray(R"([
        {"user_login":"","first_timestamp":"","last_timestamp":""},
        {"user_login":"MiXeDcAsE",
         "first_timestamp":"2020-01-01T00:00:00Z",
         "last_timestamp":"2021-01-01T00:00:00Z"}
    ])");

    auto history = parseTwitchNameHistory(root, "2", "mixedcase");

    EXPECT_EQ(history.currentLogin, "mixedcase");
    ASSERT_EQ(history.entries.size(), 1);
    EXPECT_EQ(history.entries[0].login, "mixedcase");
}

TEST(TwitchNameHistory, fallsBackToRequestedLoginWhenEmpty)
{
    auto history = parseTwitchNameHistory(QJsonArray{}, "3", "SomeUser");

    EXPECT_EQ(history.currentLogin, "someuser");
    EXPECT_TRUE(history.entries.empty());
}

TEST(TwitchNameHistory, reportsUnknownForUnparsableTimestamps)
{
    auto root = parseArray(R"([
        {"user_login":"a","first_timestamp":"not a date",
         "last_timestamp":"also not a date"},
        {"user_login":"b","first_timestamp":"2020-01-01T00:00:00Z",
         "last_timestamp":"2021-01-01T00:00:00Z"}
    ])");

    auto history = parseTwitchNameHistory(root, "4", "b");

    ASSERT_EQ(history.entries.size(), 2);
    EXPECT_EQ(history.entries[1].login, "a");
    EXPECT_EQ(history.entries[1].firstSeen, "Unknown");
}

TEST(TwitchNameHistory, capsEntryCount)
{
    QJsonArray root;
    for (int i = 0; i < TWITCH_NAME_HISTORY_LIMIT + 20; i++)
    {
        root.append(QJsonObject{
            {"user_login", QString("user%1").arg(i)},
            {"first_timestamp", "2020-01-01T00:00:00Z"},
            {"last_timestamp",
             QString("20%1-01-01T00:00:00Z").arg(20 + i, 2, 10, QChar('0'))},
        });
    }

    auto history = parseTwitchNameHistory(root, "5", "user0");

    EXPECT_EQ(static_cast<int>(history.entries.size()),
              TWITCH_NAME_HISTORY_LIMIT);
}
