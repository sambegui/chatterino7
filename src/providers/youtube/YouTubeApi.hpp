#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace chatterino {

class YouTubeAccount;

/// The official YouTube Data API v3 client, used only for writes.
///
/// Reading chat here would be unusable: liveChatMessages.list costs 5 quota
/// units and the poll interval is a few seconds, which burns the default
/// 10,000 units/day allowance in about three hours on a single stream. Sending
/// is the opposite: 50 units per message is roughly 200 messages a day, which
/// is more than anyone types, and it keeps posting on the supported API with a
/// real OAuth grant instead of replaying browser cookies.
class YouTubeApi : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(YouTubeApi)

public:
    explicit YouTubeApi(QObject *parent = nullptr);
    ~YouTubeApi() override;

    void setAccount(std::shared_ptr<YouTubeAccount> account);

    /// videos.list, 1 quota unit. The live chat id is required to post and is
    /// not exposed on the watch page.
    void resolveLiveChatId(
        const QString &videoId,
        std::function<void(QString liveChatId, QString error)> cb);

    /// liveChatMessages.insert, 50 quota units.
    void sendMessage(const QString &liveChatId, const QString &text,
                     std::function<void(bool ok, QString error)> cb);

    /// Exchanges an authorization code for tokens, then fills in the account's
    /// channel identity.
    void exchangeCode(const QString &code, const QString &redirectUri,
                      std::function<void(bool ok, QString error)> cb);

    /// Uses the stored refresh token to mint a new access token.
    void refreshToken(std::function<void(bool ok, QString error)> cb);

    /// The OAuth client credentials, read from the
    /// CHATTERINO_YOUTUBE_CLIENT_ID / _SECRET environment variables, matching
    /// how the Kick integration is configured.
    static QString clientId();
    static QString clientSecret();
    static bool hasClientCredentials();

private:
    /// Runs @a work with a valid access token, refreshing first if needed.
    void withFreshToken(std::function<void(bool ok, QString error)> onFailure,
                        std::function<void(QString token)> work);

    QNetworkAccessManager *network_;
    std::shared_ptr<YouTubeAccount> account_;
};

}  // namespace chatterino
