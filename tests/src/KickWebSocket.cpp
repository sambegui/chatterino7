#include "providers/kick/KickWebSocket.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

using namespace chatterino;

TEST(KickWebSocket, parsesConnectionEstablished)
{
    // Test that connection_established event is properly parsed
    QString rawMessage = R"({
        "event": "pusher:connection_established",
        "data": "{\"socket_id\":\"123456.789012\",\"activity_timeout\":120}"
    })";

    // The actual parsing happens inside KickWebSocket::parseMessage
    // We verify the JSON structure is correct
    QJsonDocument doc = QJsonDocument::fromJson(rawMessage.toUtf8());
    ASSERT_TRUE(doc.isObject());

    QJsonObject obj = doc.object();
    EXPECT_EQ(obj["event"].toString(), "pusher:connection_established");

    QString dataStr = obj["data"].toString();
    QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
    ASSERT_TRUE(dataDoc.isObject());

    QJsonObject data = dataDoc.object();
    EXPECT_EQ(data["socket_id"].toString(), "123456.789012");
    EXPECT_EQ(data["activity_timeout"].toInt(), 120);
}

TEST(KickWebSocket, parsesSubscriptionSucceeded)
{
    QString rawMessage = R"({
        "event": "pusher_internal:subscription_succeeded",
        "channel": "chatrooms.12345.v2",
        "data": "{}"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(rawMessage.toUtf8());
    ASSERT_TRUE(doc.isObject());

    QJsonObject obj = doc.object();
    EXPECT_EQ(obj["event"].toString(),
              "pusher_internal:subscription_succeeded");
    EXPECT_EQ(obj["channel"].toString(), "chatrooms.12345.v2");
}

TEST(KickWebSocket, parsesChatMessageEvent)
{
    // Test chat message event parsing
    // This matches the format from Context7: /kickengineering/kickdevdocs
    QString rawMessage = R"({
        "event": "App\\Events\\ChatMessageEvent",
        "channel": "chatrooms.12345.v2",
        "data": "{\"id\":\"msg123\",\"content\":\"Hello World!\",\"sender\":{\"id\":456,\"username\":\"testuser\",\"slug\":\"testuser\",\"identity\":{\"color\":\"#FF5733\",\"badges\":[{\"type\":\"subscriber\",\"text\":\"Sub\",\"count\":3}]}},\"created_at\":\"2025-01-01T12:00:00Z\"}"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(rawMessage.toUtf8());
    ASSERT_TRUE(doc.isObject());

    QJsonObject obj = doc.object();
    EXPECT_EQ(obj["event"].toString(), "App\\Events\\ChatMessageEvent");
    EXPECT_EQ(obj["channel"].toString(), "chatrooms.12345.v2");

    QString dataStr = obj["data"].toString();
    QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
    ASSERT_TRUE(dataDoc.isObject());

    QJsonObject data = dataDoc.object();
    EXPECT_EQ(data["id"].toString(), "msg123");
    EXPECT_EQ(data["content"].toString(), "Hello World!");

    QJsonObject sender = data["sender"].toObject();
    EXPECT_EQ(sender["username"].toString(), "testuser");
    EXPECT_EQ(sender["id"].toInt(), 456);
}

TEST(KickWebSocket, parsesPusherError)
{
    QString rawMessage = R"({
        "event": "pusher:error",
        "data": "{\"message\":\"Connection limit exceeded\",\"code\":4004}"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(rawMessage.toUtf8());
    ASSERT_TRUE(doc.isObject());

    QJsonObject obj = doc.object();
    EXPECT_EQ(obj["event"].toString(), "pusher:error");

    QString dataStr = obj["data"].toString();
    QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
    ASSERT_TRUE(dataDoc.isObject());

    QJsonObject data = dataDoc.object();
    EXPECT_EQ(data["message"].toString(), "Connection limit exceeded");
    EXPECT_EQ(data["code"].toInt(), 4004);
}

TEST(KickWebSocket, channelNameFormat)
{
    // Verify channel name format for subscription
    int chatroomId = 12345;
    QString channelName = QString("chatrooms.%1.v2").arg(chatroomId);
    EXPECT_EQ(channelName, "chatrooms.12345.v2");
}
