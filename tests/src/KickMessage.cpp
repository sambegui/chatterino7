#include "providers/kick/KickMessage.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

TEST(KickMessage, parsesBasicMessage)
{
    QString jsonStr = R"({
        "id": "msg123",
        "content": "Hello World!",
        "sender": {
            "id": 456,
            "username": "testuser",
            "slug": "testuser",
            "identity": {
                "color": "#FF5733",
                "badges": []
            }
        },
        "created_at": "2025-01-01T12:00:00Z"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    ASSERT_TRUE(doc.isObject());

    KickMessage msg = KickMessage::fromJson(doc.object());

    EXPECT_EQ(msg.id, "msg123");
    EXPECT_EQ(msg.content, "Hello World!");
    EXPECT_EQ(msg.sender.id, 456);
    EXPECT_EQ(msg.sender.username, "testuser");
    EXPECT_EQ(msg.sender.slug, "testuser");
    EXPECT_EQ(msg.sender.identity.color, "#FF5733");
}

TEST(KickMessage, parsesSenderWithBadges)
{
    QString jsonStr = R"({
        "id": "msg456",
        "content": "Badge test",
        "sender": {
            "id": 789,
            "username": "moduser",
            "slug": "moduser",
            "identity": {
                "color": "#00FF00",
                "badges": [
                    {"type": "moderator", "text": "Mod"},
                    {"type": "subscriber", "text": "Sub", "count": 6},
                    {"type": "vip", "text": "VIP"}
                ]
            }
        },
        "created_at": "2025-01-01T12:00:00Z"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    KickMessage msg = KickMessage::fromJson(doc.object());

    EXPECT_EQ(msg.sender.identity.badges.size(), 3);

    // Check moderator badge
    EXPECT_EQ(msg.sender.identity.badges[0].type, "moderator");
    EXPECT_EQ(msg.sender.identity.badges[0].text, "Mod");

    // Check subscriber badge with count
    EXPECT_EQ(msg.sender.identity.badges[1].type, "subscriber");
    EXPECT_EQ(msg.sender.identity.badges[1].count, 6);

    // Check VIP badge
    EXPECT_EQ(msg.sender.identity.badges[2].type, "vip");
}

TEST(KickMessage, handlesEmptyIdentity)
{
    QString jsonStr = R"({
        "id": "msg789",
        "content": "No identity",
        "sender": {
            "id": 123,
            "username": "anonymous",
            "slug": "anonymous",
            "identity": null
        },
        "created_at": "2025-01-01T12:00:00Z"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    KickMessage msg = KickMessage::fromJson(doc.object());

    EXPECT_EQ(msg.sender.username, "anonymous");
    EXPECT_TRUE(msg.sender.identity.color.isEmpty());
    EXPECT_TRUE(msg.sender.identity.badges.empty());
}

TEST(KickMessage, parsesTimestamp)
{
    QString jsonStr = R"({
        "id": "msg999",
        "content": "Timestamp test",
        "sender": {
            "id": 111,
            "username": "timeuser",
            "slug": "timeuser",
            "identity": {"color": "", "badges": []}
        },
        "created_at": "2025-06-15T14:30:45Z"
    })";

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    KickMessage msg = KickMessage::fromJson(doc.object());

    EXPECT_TRUE(msg.createdAt.isValid());
    EXPECT_EQ(msg.createdAt.date().year(), 2025);
    EXPECT_EQ(msg.createdAt.date().month(), 6);
    EXPECT_EQ(msg.createdAt.date().day(), 15);
}

TEST(KickBadge, parsesFromJson)
{
    QString jsonStr = R"({
        "type": "subscriber",
        "text": "12 Months",
        "count": 12
    })";

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    KickBadge badge = KickBadge::fromJson(doc.object());

    EXPECT_EQ(badge.type, "subscriber");
    EXPECT_EQ(badge.text, "12 Months");
    EXPECT_EQ(badge.count, 12);
}
