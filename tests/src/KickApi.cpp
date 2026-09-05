#include "providers/kick/KickApi.hpp"

#include "Test.hpp"

#include <QString>

using namespace chatterino;

TEST(KickApi, createsSuccessfully)
{
    KickApi api;
    // Should not crash on construction
    EXPECT_TRUE(true);
}

TEST(KickApi, accountIsNullByDefault)
{
    KickApi api;
    EXPECT_EQ(api.getAccount(), nullptr);
}

TEST(KickApi, notRateLimitedByDefault)
{
    KickApi api;
    EXPECT_FALSE(api.isRateLimited());
}

TEST(KickApi, rateLimitInfoDefaultsToZero)
{
    KickApi api;
    auto info = api.getRateLimitInfo();

    EXPECT_EQ(info.limit, 0);
    EXPECT_EQ(info.remaining, 0);
}

TEST(KickApiResult, defaultsToFailure)
{
    KickApiResult result;

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.httpStatus, 0);
    EXPECT_TRUE(result.errorMessage.isEmpty());
}

TEST(KickApiResult, canBeSetToSuccess)
{
    KickApiResult result;
    result.success = true;
    result.httpStatus = 200;

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.httpStatus, 200);
}

TEST(KickRateLimitInfo, defaultConstruction)
{
    KickRateLimitInfo info;

    EXPECT_EQ(info.limit, 0);
    EXPECT_EQ(info.remaining, 0);
    EXPECT_FALSE(info.resetAt.isValid());
}

TEST(KickApiEndpoints, baseUrlIsCorrect)
{
    // Verify API endpoint matches Context7 documentation
    // https://api.kick.com/public/v1/chat
    QString expectedBase = "https://api.kick.com/public/v1";
    QString actualBase = QString::fromLatin1(KickApi::KICK_API_BASE);

    EXPECT_EQ(actualBase, expectedBase);
}
