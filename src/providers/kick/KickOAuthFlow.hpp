#pragma once

#include <pajlada/signals/signal.hpp>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTimer>

#include <memory>
#include <optional>

namespace chatterino {

/// OAuth 2.1 with PKCE authentication flow for Kick.com
class KickOAuthFlow : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KickOAuthFlow)

public:
    explicit KickOAuthFlow(QObject *parent = nullptr);
    ~KickOAuthFlow() override;

    /// OAuth tokens received from Kick
    struct Tokens {
        QString accessToken;
        QString refreshToken;
        QDateTime expiresAt;
        QString scope;
    };

    /// Start the OAuth flow
    /// Opens system browser for user authorization
    /// @return true if flow started successfully
    bool start();

    /// Cancel the ongoing OAuth flow
    void cancel();

    /// Check if OAuth flow is in progress
    [[nodiscard]] bool isInProgress() const;

    /// Signal emitted when authentication succeeds
    pajlada::Signals::Signal<Tokens> authenticationSuccess;

    /// Signal emitted when authentication fails
    pajlada::Signals::Signal<QString> authenticationFailed;

    // Kick OAuth configuration (from Context7: /kickengineering/kickdevdocs)
    // Authorization endpoint: https://id.kick.com/oauth/authorize
    // Token endpoint: https://id.kick.com/oauth/token
    static constexpr const char *KICK_AUTH_URL =
        "https://id.kick.com/oauth/authorize";
    static constexpr const char *KICK_TOKEN_URL =
        "https://id.kick.com/oauth/token";
    static constexpr const char *REDIRECT_URI =
        "http://localhost:52847/callback";
    static constexpr int LOCAL_SERVER_PORT = 52847;

    /// How long to wait for the browser round trip before giving up
    static constexpr int AUTH_TIMEOUT_MS = 300000;

private Q_SLOTS:
    void onNewConnection();
    void onClientReadyRead();

private:
    /// Generate PKCE code verifier and challenge
    void generatePKCE();

    /// Build the authorization URL with all required parameters
    [[nodiscard]] QString buildAuthorizationUrl() const;

    /// Start local HTTP server for OAuth callback
    bool startLocalServer();

    /// Stop local HTTP server
    void stopLocalServer();

    /// Open system browser with authorization URL
    bool openBrowser(const QString &url);

    /// Exchange authorization code for tokens
    void exchangeCodeForTokens(const QString &code);

    /// Parse HTTP request from callback
    [[nodiscard]] std::optional<QString> parseCallbackCode(
        const QByteArray &httpRequest) const;

    /// Send HTTP response to browser
    void sendResponse(QTcpSocket *socket, int statusCode,
                      const QString &message);

    // PKCE values
    QString codeVerifier_;
    QString codeChallenge_;
    QString state_;

    // Server for OAuth callback
    std::unique_ptr<QTcpServer> localServer_;
    QTimer timeoutTimer_;
    bool isInProgress_{false};
};

}  // namespace chatterino
