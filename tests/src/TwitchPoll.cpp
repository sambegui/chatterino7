#include "providers/twitch/TwitchPoll.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

/// Builds the {type, topic, data} envelope PubSubClient hands to the channel.
QJsonObject payload(const QString &type, const char *data)
{
    return QJsonObject{
        {"type", type},
        {"topic", "polls.71092938"},
        {"data", QJsonDocument::fromJson(data).object()},
    };
}

const char *POLL = R"({"poll":{
    "poll_id":"poll-1",
    "title":"which game next",
    "status":"ACTIVE",
    "ends_at":"2026-09-02T23:00:00Z",
    "choices":[
        {"choice_id":"a","title":"Elden Ring","votes":{"total":70}},
        {"choice_id":"b","title":"Minecraft","votes":{"total":30}}
    ]
}})";

}  // namespace

TEST(TwitchPoll, parsesAnActivePoll)
{
    auto poll = parseTwitchPoll(payload("POLL_UPDATE", POLL));

    ASSERT_TRUE(poll.has_value());
    EXPECT_EQ(poll->id, "poll-1");
    EXPECT_EQ(poll->title, "which game next");
    EXPECT_TRUE(poll->isActive());
    EXPECT_EQ(poll->totalVotes, 100);
    EXPECT_TRUE(poll->endsAt.isValid());

    ASSERT_EQ(poll->choices.size(), 2);
    EXPECT_EQ(poll->choices[0].title, "Elden Ring");
    EXPECT_EQ(poll->choices[0].votes, 70);
    EXPECT_EQ(poll->choices[1].votes, 30);
}

TEST(TwitchPoll, acceptsAnUnnestedBody)
{
    // some events send the poll as the data object itself
    auto poll = parseTwitchPoll(payload("POLL_CREATE", R"({
        "id":"poll-2","title":"yes or no","status":"ACTIVE",
        "choices":[{"id":"a","title":"yes","votes":{"total":1}},
                   {"id":"b","title":"no","votes":{"total":0}}]
    })"));

    ASSERT_TRUE(poll.has_value());
    EXPECT_EQ(poll->id, "poll-2");
    EXPECT_EQ(poll->choices[0].id, "a");
}

TEST(TwitchPoll, defaultsStatusFromTheEventType)
{
    auto ended = parseTwitchPoll(payload("POLL_END", R"({"poll":{
        "poll_id":"poll-3","title":"t",
        "choices":[{"choice_id":"a","title":"a","votes":{"total":2}},
                   {"choice_id":"b","title":"b","votes":{"total":1}}]}})"));

    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->status, "COMPLETED");
    EXPECT_FALSE(ended->isActive());
    // an end with no closing time is treated as closing now
    EXPECT_TRUE(ended->endsAt.isValid());

    auto running = parseTwitchPoll(payload("POLL_UPDATE", R"({"poll":{
        "poll_id":"poll-4","title":"t",
        "choices":[{"choice_id":"a","title":"a","votes":{"total":0}},
                   {"choice_id":"b","title":"b","votes":{"total":0}}]}})"));

    ASSERT_TRUE(running.has_value());
    EXPECT_TRUE(running->isActive());
}

TEST(TwitchPoll, rejectsIncompletePayloads)
{
    EXPECT_FALSE(parseTwitchPoll(payload("POLL_UPDATE", "{}")).has_value());
    // no choices is not a poll worth showing
    EXPECT_FALSE(
        parseTwitchPoll(
            payload("POLL_UPDATE", R"({"poll":{"poll_id":"x","title":"t"}})"))
            .has_value());
    // nor is one with no title
    EXPECT_FALSE(parseTwitchPoll(payload("POLL_UPDATE",
                                         R"({"poll":{"poll_id":"x","choices":[
                                    {"choice_id":"a","title":"a",
                                     "votes":{"total":0}}]}})"))
                     .has_value());
}

TEST(TwitchPoll, handlesZeroVotesWithoutDividing)
{
    auto poll = parseTwitchPoll(payload("POLL_CREATE", R"({"poll":{
        "poll_id":"poll-5","title":"fresh","status":"ACTIVE",
        "choices":[{"choice_id":"a","title":"a","votes":{"total":0}},
                   {"choice_id":"b","title":"b","votes":{"total":0}}]}})"));

    ASSERT_TRUE(poll.has_value());
    EXPECT_EQ(poll->totalVotes, 0);
}
