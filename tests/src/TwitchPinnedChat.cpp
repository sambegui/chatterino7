#include "providers/twitch/TwitchPinnedChat.hpp"

#include "Test.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

QJsonValue parseJson(const char *json)
{
    auto doc = QJsonDocument::fromJson(json);
    if (doc.isArray())
    {
        return doc.array();
    }
    return doc.object();
}

// the shape gql answers GetPinnedChat with
const char *PINNED = R"([{"data":{"channel":{
    "id":"71092938",
    "pinnedChatMessages":{"edges":[{"node":{
        "id":"pin-1",
        "endsAt":"2026-09-02T23:00:00Z",
        "updatedAt":"2026-09-02T22:00:00Z",
        "pinnedBy":{"login":"themod","displayName":"TheMod"},
        "pinnedMessage":{
            "id":"msg-1",
            "content":{"text":"read the rules"},
            "sender":{"id":"5","login":"someone",
                      "displayName":"SomeOne","chatColor":"#FF0000"}
        }
    }}]}
}}}])";

}  // namespace

TEST(TwitchPinnedChat, parsesAPinnedMessage)
{
    auto pin = parseTwitchPinnedChat(parseJson(PINNED));

    ASSERT_TRUE(pin.has_value());
    EXPECT_EQ(pin->pinId, "pin-1");
    EXPECT_EQ(pin->messageId, "msg-1");
    EXPECT_EQ(pin->text, "read the rules");
    EXPECT_EQ(pin->authorName, "SomeOne");
    EXPECT_EQ(pin->authorLogin, "someone");
    EXPECT_EQ(pin->authorColor, "#FF0000");
    EXPECT_EQ(pin->pinnerName, "TheMod");
    EXPECT_TRUE(pin->endsAt.isValid());
    EXPECT_TRUE(pin->pinnedAt.isValid());
}

TEST(TwitchPinnedChat, returnsNothingWhenNoPinIsSet)
{
    // what the live endpoint answers for a channel with no pin
    auto pin = parseTwitchPinnedChat(parseJson(
        R"([{"data":{"channel":{"id":"71092938",
            "pinnedChatMessages":{"edges":[],
            "pageInfo":{"hasNextPage":false}}}}}])"));

    EXPECT_FALSE(pin.has_value());
}

TEST(TwitchPinnedChat, acceptsAnUnwrappedObject)
{
    auto pin = parseTwitchPinnedChat(parseJson(
        R"({"data":{"channel":{"pinnedChatMessages":{"edges":[{"node":{
            "id":"pin-2",
            "pinnedMessage":{"content":{"text":"hello"},
                             "sender":{"displayName":"Name"}}
        }}]}}}})"));

    ASSERT_TRUE(pin.has_value());
    EXPECT_EQ(pin->pinId, "pin-2");
    EXPECT_EQ(pin->text, "hello");
    // a pin without an expiry stays until it is removed
    EXPECT_FALSE(pin->endsAt.isValid());
    // and one without a timestamp is treated as pinned now
    EXPECT_TRUE(pin->pinnedAt.isValid());
}

TEST(TwitchPinnedChat, fallsBackToLoginWhenDisplayNameIsMissing)
{
    auto pin = parseTwitchPinnedChat(parseJson(
        R"({"data":{"channel":{"pinnedChatMessages":{"edges":[{"node":{
            "id":"pin-3",
            "pinnedBy":{"login":"modlogin"},
            "pinnedMessage":{"content":{"text":"hi"},
                             "sender":{"login":"userlogin"}}
        }}]}}}})"));

    ASSERT_TRUE(pin.has_value());
    EXPECT_EQ(pin->authorLogin, "userlogin");
    EXPECT_EQ(pin->pinnerName, "modlogin");
}

TEST(TwitchPinnedChat, rejectsMalformedResponses)
{
    EXPECT_FALSE(parseTwitchPinnedChat(parseJson("{}")).has_value());
    EXPECT_FALSE(parseTwitchPinnedChat(parseJson("[]")).has_value());
    EXPECT_FALSE(parseTwitchPinnedChat(QJsonValue{}).has_value());
    // an edge with no text is not something worth showing
    EXPECT_FALSE(
        parseTwitchPinnedChat(parseJson(
                                  R"({"data":{"channel":{"pinnedChatMessages":
                         {"edges":[{"node":{"id":"pin-4"}}]}}}})"))
            .has_value());
}
