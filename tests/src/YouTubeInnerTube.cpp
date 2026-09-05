#include "providers/youtube/YouTubeInnerTube.hpp"

#include "Test.hpp"

using namespace chatterino;

namespace {

/// resolveVideoId answers synchronously for everything that already contains a
/// video id; only handles and channel URLs need a network round trip, and those
/// are not exercised here.
struct Resolved {
    QString videoId;
    QString error;
    bool called{false};
};

Resolved resolveSync(YouTubeInnerTube &tube, const QString &input)
{
    Resolved out;
    tube.resolveVideoId(input, [&out](QString id, QString error) {
        out.videoId = std::move(id);
        out.error = std::move(error);
        out.called = true;
    });
    return out;
}

}  // namespace

TEST(YouTubeInnerTube, resolvesBareVideoId)
{
    YouTubeInnerTube tube;

    auto r = resolveSync(tube, "jfKfPfyJRdk");
    ASSERT_TRUE(r.called);
    EXPECT_EQ(r.videoId, "jfKfPfyJRdk");
    EXPECT_TRUE(r.error.isEmpty());
}

TEST(YouTubeInnerTube, resolvesWatchUrls)
{
    YouTubeInnerTube tube;

    for (const auto *input : {
             "https://www.youtube.com/watch?v=jfKfPfyJRdk",
             "http://youtube.com/watch?v=jfKfPfyJRdk",
             "www.youtube.com/watch?v=jfKfPfyJRdk",
             "youtube.com/watch?v=jfKfPfyJRdk",
             "https://m.youtube.com/watch?v=jfKfPfyJRdk",
         })
    {
        auto r = resolveSync(tube, input);
        ASSERT_TRUE(r.called) << input;
        EXPECT_EQ(r.videoId, "jfKfPfyJRdk") << input;
    }
}

TEST(YouTubeInnerTube, resolvesWatchUrlWithExtraQueryParams)
{
    YouTubeInnerTube tube;

    // A shared link usually carries a timestamp and a tracking parameter.
    auto r = resolveSync(
        tube, "https://www.youtube.com/watch?v=jfKfPfyJRdk&t=42s&feature=share");
    ASSERT_TRUE(r.called);
    EXPECT_EQ(r.videoId, "jfKfPfyJRdk");
}

TEST(YouTubeInnerTube, resolvesShortAndLiveUrls)
{
    YouTubeInnerTube tube;

    auto shortUrl = resolveSync(tube, "https://youtu.be/jfKfPfyJRdk?t=10");
    ASSERT_TRUE(shortUrl.called);
    EXPECT_EQ(shortUrl.videoId, "jfKfPfyJRdk");

    auto liveUrl = resolveSync(tube, "https://www.youtube.com/live/jfKfPfyJRdk");
    ASSERT_TRUE(liveUrl.called);
    EXPECT_EQ(liveUrl.videoId, "jfKfPfyJRdk");
}

TEST(YouTubeInnerTube, rejectsEmptyInput)
{
    YouTubeInnerTube tube;

    auto r = resolveSync(tube, "   ");
    ASSERT_TRUE(r.called);
    EXPECT_TRUE(r.videoId.isEmpty());
    EXPECT_FALSE(r.error.isEmpty());
}

TEST(YouTubeInnerTube, sessionIsInvalidWithoutKeyOrContinuation)
{
    YouTubeChatSession session;
    EXPECT_FALSE(session.valid());

    session.apiKey = "AIza-not-a-real-key";
    EXPECT_FALSE(session.valid());

    session.continuation = "token";
    EXPECT_TRUE(session.valid());
}

TEST(YouTubeInnerTube, plainTextRendersEmojiAsTheirLabel)
{
    YouTubeMessage msg;
    msg.runs.push_back({.text = "hello "});
    msg.runs.push_back({.emojiUrl = "https://example.invalid/e.png",
                        .emojiLabel = ":_hype:",
                        .isCustomEmoji = true});
    msg.runs.push_back({.text = " world"});

    EXPECT_EQ(msg.plainText(), "hello :_hype: world");
}
