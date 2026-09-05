#pragma once

#include <QDateTime>
#include <QString>

namespace chatterino {

/// The Google account used to *send* YouTube messages.
///
/// Reading live chat is anonymous, so this is only ever needed once the user
/// wants to talk. Tokens come from the official OAuth flow and are refreshed
/// against Google's token endpoint.
class YouTubeAccount
{
public:
    YouTubeAccount() = default;

    [[nodiscard]] bool isAuthenticated() const;
    /// True when the access token is gone or about to expire.
    [[nodiscard]] bool needsRefresh() const;

    [[nodiscard]] const QString &getAccessToken() const;
    [[nodiscard]] const QString &getRefreshToken() const;
    [[nodiscard]] const QString &getChannelId() const;
    [[nodiscard]] const QString &getDisplayName() const;

    void setTokens(const QString &accessToken, const QString &refreshToken,
                   int expiresInSeconds);
    void setIdentity(const QString &channelId, const QString &displayName);
    void clear();

    /// Persists to and loads from the settings store. The refresh token is the
    /// only long-lived secret here.
    void save() const;
    void load();

private:
    QString accessToken_;
    QString refreshToken_;
    QString channelId_;
    QString displayName_;
    QDateTime expiresAt_;
};

}  // namespace chatterino
