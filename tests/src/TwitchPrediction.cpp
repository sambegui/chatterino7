#include "providers/twitch/TwitchPrediction.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

QJsonObject payload(const QString &type, const char *data)
{
    return QJsonObject{
        {"type", type},
        {"topic", "predictions-channel-v1.71092938"},
        {"data", QJsonDocument::fromJson(data).object()},
    };
}

const char *PREDICTION = R"({"event":{
    "id":"pred-1",
    "title":"will he win",
    "status":"ACTIVE",
    "created_at":"2026-09-02T22:00:00Z",
    "prediction_window_seconds":120,
    "outcomes":[
        {"id":"blue","title":"yes","total_points":8000,"total_users":40},
        {"id":"pink","title":"no","total_points":2000,"total_users":10}
    ]
}})";

}  // namespace

TEST(TwitchPrediction, parsesAnActivePrediction)
{
    auto prediction =
        parseTwitchPrediction(payload("event-updated", PREDICTION));

    ASSERT_TRUE(prediction.has_value());
    EXPECT_EQ(prediction->id, "pred-1");
    EXPECT_EQ(prediction->title, "will he win");
    EXPECT_TRUE(prediction->isActive());
    EXPECT_EQ(prediction->totalPoints, 10000);

    ASSERT_EQ(prediction->outcomes.size(), 2);
    EXPECT_EQ(prediction->outcomes[0].points, 8000);
    EXPECT_EQ(prediction->outcomes[0].users, 40);
}

TEST(TwitchPrediction, assignsOutcomeColorsByPosition)
{
    auto two = parseTwitchPrediction(payload("event-created", PREDICTION));
    ASSERT_TRUE(two.has_value());
    EXPECT_EQ(two->outcomes[0].color, "BLUE");
    EXPECT_EQ(two->outcomes[1].color, "PINK");

    auto three = parseTwitchPrediction(payload("event-created", R"({"event":{
        "id":"p","title":"t","status":"ACTIVE","outcomes":[
            {"id":"1","title":"a"},{"id":"2","title":"b"},{"id":"3","title":"c"}
        ]}})"));
    ASSERT_TRUE(three.has_value());
    EXPECT_EQ(three->outcomes[0].color, "BLUE");
    EXPECT_EQ(three->outcomes[1].color, "PINK");
    EXPECT_EQ(three->outcomes[2].color, "GREEN");
}

TEST(TwitchPrediction, prefersAServerSentColor)
{
    auto prediction =
        parseTwitchPrediction(payload("event-created", R"({"event":{
        "id":"p","title":"t","status":"ACTIVE","outcomes":[
            {"id":"1","title":"a","color":"GREEN"},
            {"id":"2","title":"b","color":"BLUE"}
        ]}})"));

    ASSERT_TRUE(prediction.has_value());
    EXPECT_EQ(prediction->outcomes[0].color, "GREEN");
    EXPECT_EQ(prediction->outcomes[1].color, "BLUE");
}

TEST(TwitchPrediction, computesWhenEntriesClose)
{
    auto prediction =
        parseTwitchPrediction(payload("event-created", PREDICTION));

    ASSERT_TRUE(prediction.has_value());
    auto locksAt = prediction->locksAt();
    ASSERT_TRUE(locksAt.isValid());
    EXPECT_EQ(prediction->createdAt.secsTo(locksAt), 120);
}

TEST(TwitchPrediction, hasNoCloseTimeWithoutAWindow)
{
    auto prediction =
        parseTwitchPrediction(payload("event-created", R"({"event":{
        "id":"p","title":"t","status":"ACTIVE","outcomes":[
            {"id":"1","title":"a"},{"id":"2","title":"b"}]}})"));

    ASSERT_TRUE(prediction.has_value());
    EXPECT_FALSE(prediction->locksAt().isValid());
}

TEST(TwitchPrediction, readsTheWinningOutcome)
{
    auto prediction =
        parseTwitchPrediction(payload("event-resolved", R"({"event":{
        "id":"p","title":"t","status":"RESOLVED","winning_outcome_id":"pink",
        "outcomes":[{"id":"blue","title":"a"},{"id":"pink","title":"b"}]}})"));

    ASSERT_TRUE(prediction.has_value());
    EXPECT_EQ(prediction->winningOutcomeId, "pink");
    EXPECT_FALSE(prediction->isActive());
}

TEST(TwitchPrediction, rejectsIncompletePayloads)
{
    EXPECT_FALSE(
        parseTwitchPrediction(payload("event-updated", "{}")).has_value());
    EXPECT_FALSE(
        parseTwitchPrediction(
            payload("event-updated", R"({"event":{"id":"p","title":"t"}})"))
            .has_value());
}
