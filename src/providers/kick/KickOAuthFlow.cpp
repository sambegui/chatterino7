#include "providers/kick/KickOAuthFlow.hpp"

#include "common/QLogging.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <array>

namespace chatterino {

KickOAuthFlow::KickOAuthFlow(QObject *parent)
    : QObject(parent)
{
    this->timeoutTimer_.setSingleShot(true);
    QObject::connect(&this->timeoutTimer_, &QTimer::timeout, this, [this] {
        qCWarning(chatterinoKick) << "OAuth flow timed out waiting for the "
                                     "browser callback";
        this->cancel();
        this->authenticationFailed.invoke(
            "Timed out waiting for Kick authorization");
    });
}

KickOAuthFlow::~KickOAuthFlow()
{
    this->cancel();
}

bool KickOAuthFlow::start()
{
    if (this->isInProgress_)
    {
        qCWarning(chatterinoKick) << "OAuth flow already in progress";
        return false;
    }

    // Generate PKCE values
    this->generatePKCE();

    // Start local server for callback
    if (!this->startLocalServer())
    {
        this->authenticationFailed.invoke("Failed to start local server for "
                                          "OAuth callback");
        return false;
    }

    // Build authorization URL
    QString authUrl = this->buildAuthorizationUrl();

    // Open browser
    if (!this->openBrowser(authUrl))
    {
        this->stopLocalServer();
        this->authenticationFailed.invoke("Failed to open browser for "
                                          "authentication");
        return false;
    }

    this->isInProgress_ = true;
    this->timeoutTimer_.start(AUTH_TIMEOUT_MS);
    qCDebug(chatterinoKick) << "OAuth flow started, waiting for callback...";
    return true;
}

void KickOAuthFlow::cancel()
{
    this->timeoutTimer_.stop();
    this->stopLocalServer();
    this->isInProgress_ = false;
    this->codeVerifier_.clear();
    this->codeChallenge_.clear();
    this->state_.clear();
}

bool KickOAuthFlow::isInProgress() const
{
    return this->isInProgress_;
}

void KickOAuthFlow::generatePKCE()
{
    // Generate random code verifier (43-128 characters)
    const int verifierLength = 64;
    const QString charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

    this->codeVerifier_.clear();
    this->codeVerifier_.reserve(verifierLength);

    for (int i = 0; i < verifierLength; ++i)
    {
        int index = QRandomGenerator::global()->bounded(charset.length());
        this->codeVerifier_.append(charset[index]);
    }

    // Generate code challenge (SHA256 hash, base64url encoded)
    QByteArray hash = QCryptographicHash::hash(this->codeVerifier_.toUtf8(),
                                               QCryptographicHash::Sha256);
    this->codeChallenge_ = hash.toBase64(QByteArray::Base64UrlEncoding |
                                         QByteArray::OmitTrailingEquals);

    // Generate random state for CSRF protection
    QByteArray stateBytes(32, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32 *>(stateBytes.data()), 8);
    this->state_ = stateBytes.toBase64(QByteArray::Base64UrlEncoding |
                                       QByteArray::OmitTrailingEquals);

    qCDebug(chatterinoKick) << "Generated PKCE code verifier and challenge";
}

QString KickOAuthFlow::buildAuthorizationUrl() const
{
    QUrl url(KICK_AUTH_URL);
    QUrlQuery query;

    // Get client ID: compile-time define > environment variable > fallback
    QString clientId;
#ifdef CHATTERINO_KICK_CLIENT_ID
    clientId = QStringLiteral(CHATTERINO_KICK_CLIENT_ID);
#endif
    if (clientId.isEmpty())
    {
        clientId = qEnvironmentVariable("CHATTERINO_KICK_CLIENT_ID", "");
    }
    if (clientId.isEmpty())
    {
        qCWarning(chatterinoKick)
            << "No Kick Client ID configured. Kick OAuth will not work. "
               "Set CHATTERINO_KICK_CLIENT_ID environment variable or rebuild "
               "with -DCHATTERINO_KICK_CLIENT_ID=...";
    }

    query.addQueryItem("response_type", "code");
    query.addQueryItem("client_id", clientId);
    query.addQueryItem("redirect_uri", REDIRECT_URI);
    query.addQueryItem("scope",
                       "chat:write user:read channel:read moderation:ban");
    query.addQueryItem("state", this->state_);
    query.addQueryItem("code_challenge", this->codeChallenge_);
    query.addQueryItem("code_challenge_method", "S256");

    url.setQuery(query);
    return url.toString();
}

bool KickOAuthFlow::startLocalServer()
{
    this->localServer_ = std::make_unique<QTcpServer>(this);

    QObject::connect(this->localServer_.get(), &QTcpServer::newConnection, this,
                     &KickOAuthFlow::onNewConnection);

    // Try the default port first
    if (this->localServer_->listen(QHostAddress::LocalHost, LOCAL_SERVER_PORT))
    {
        qCDebug(chatterinoKick)
            << "OAuth callback server listening on port" << LOCAL_SERVER_PORT;
        return true;
    }

    // Port conflict - try alternative ports
    qCWarning(chatterinoKick)
        << "Port" << LOCAL_SERVER_PORT
        << "is busy:" << this->localServer_->errorString();

    // Try a few alternative ports
    static const std::array<quint16, 3> alternativePorts = {9001, 9002, 9003};

    for (quint16 port : alternativePorts)
    {
        if (this->localServer_->listen(QHostAddress::LocalHost, port))
        {
            qCDebug(chatterinoKick)
                << "OAuth callback server listening on alternative port"
                << port;
            // Note: The redirect URI must match what's registered with Kick
            // If using a different port, the OAuth will fail unless Kick allows it
            qCWarning(chatterinoKick)
                << "Using alternative port may cause OAuth to fail if not "
                   "registered with Kick";
            return true;
        }
    }

    qCWarning(chatterinoKick)
        << "Failed to start local server on any port. Another instance of "
           "Chatterino may be running, or ports are blocked by firewall.";
    return false;
}

void KickOAuthFlow::stopLocalServer()
{
    if (this->localServer_)
    {
        this->localServer_->close();
        this->localServer_.reset();
    }
}

bool KickOAuthFlow::openBrowser(const QString &url)
{
    qCDebug(chatterinoKick) << "Opening browser for OAuth:" << url;

    bool opened = QDesktopServices::openUrl(QUrl(url));

    if (!opened)
    {
        qCWarning(chatterinoKick)
            << "Failed to open browser automatically. Please manually visit: "
            << url;
    }

    return opened;
}

void KickOAuthFlow::onNewConnection()
{
    QTcpSocket *socket = this->localServer_->nextPendingConnection();
    if (!socket)
    {
        return;
    }

    QObject::connect(socket, &QTcpSocket::readyRead, this,
                     &KickOAuthFlow::onClientReadyRead);
    QObject::connect(socket, &QTcpSocket::disconnected, socket,
                     &QTcpSocket::deleteLater);
}

void KickOAuthFlow::onClientReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
    {
        return;
    }

    QByteArray request = socket->readAll();
    auto code = this->parseCallbackCode(request);

    if (!code.has_value())
    {
        // Check if this was just a favicon or non-callback request
        QString requestStr = QString::fromUtf8(request);
        if (!requestStr.contains("/callback?"))
        {
            // Silently ignore non-callback requests (favicon, etc.)
            socket->disconnectFromHost();
            return;
        }

        this->sendResponse(socket, 400, "Invalid callback request");

        // Defer cancel to avoid crash (don't delete server while in callback)
        QTimer::singleShot(0, this, [this] {
            this->authenticationFailed.invoke("Invalid OAuth callback");
            this->cancel();
        });
        return;
    }

    // Send success response to browser
    this->sendResponse(socket, 200,
                       "Authentication successful! You can close this window.");

    // Exchange code for tokens
    this->exchangeCodeForTokens(*code);
}

std::optional<QString> KickOAuthFlow::parseCallbackCode(
    const QByteArray &httpRequest) const
{
    // Parse HTTP GET request for /callback?code=...&state=...
    QString requestStr = QString::fromUtf8(httpRequest);
    QStringList lines = requestStr.split("\r\n");

    if (lines.isEmpty())
    {
        return std::nullopt;
    }

    // Parse first line: GET /callback?code=xxx&state=yyy HTTP/1.1
    QString firstLine = lines.first();
    QStringList parts = firstLine.split(" ");
    if (parts.size() < 2 || parts[0] != "GET")
    {
        return std::nullopt;
    }

    QString path = parts[1];

    // Ignore favicon.ico and other non-callback requests
    if (!path.startsWith("/callback?"))
    {
        qCDebug(chatterinoKick) << "Ignoring non-callback request:" << path;
        return std::nullopt;
    }

    QUrl url("http://localhost" + path);
    QUrlQuery query(url.query());

    // Verify state to prevent CSRF (URL-decode both sides for comparison)
    QString state = query.queryItemValue("state", QUrl::FullyDecoded);
    qCDebug(chatterinoKick) << "Received state:" << state;
    qCDebug(chatterinoKick) << "Expected state:" << this->state_;

    if (state != this->state_)
    {
        qCWarning(chatterinoKick)
            << "State mismatch in OAuth callback"
            << "received:" << state << "expected:" << this->state_;
        return std::nullopt;
    }

    // Check for error
    QString error = query.queryItemValue("error");
    if (!error.isEmpty())
    {
        qCWarning(chatterinoKick) << "OAuth error:" << error
                                  << query.queryItemValue("error_description");
        return std::nullopt;
    }

    QString code = query.queryItemValue("code");
    if (code.isEmpty())
    {
        return std::nullopt;
    }

    return code;
}

void KickOAuthFlow::sendResponse(QTcpSocket *socket, int statusCode,
                                 const QString &message)
{
    QString statusText = (statusCode == 200) ? "OK" : "Bad Request";
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>Chatterino7 - Kick Authentication</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #eee;
        }
        .container {
            text-align: center;
            padding: 40px;
            background: rgba(255,255,255,0.1);
            border-radius: 16px;
            backdrop-filter: blur(10px);
        }
        h1 { color: %1; margin-bottom: 16px; }
        p { opacity: 0.8; }
    </style>
</head>
<body>
    <div class="container">
        <h1>%2</h1>
        <p>%3</p>
    </div>
</body>
</html>
)")
                       .arg(statusCode == 200 ? "#53fc18" : "#ff4444")
                       .arg(statusCode == 200 ? "✓ Success" : "✗ Error")
                       .arg(message);

    QString response = QString("HTTP/1.1 %1 %2\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "Content-Length: %3\r\n"
                               "Connection: close\r\n"
                               "\r\n"
                               "%4")
                           .arg(statusCode)
                           .arg(statusText)
                           .arg(html.toUtf8().size())
                           .arg(html);

    socket->write(response.toUtf8());
    socket->flush();
    socket->disconnectFromHost();
}

void KickOAuthFlow::exchangeCodeForTokens(const QString &code)
{
    qCDebug(chatterinoKick) << "Exchanging authorization code for tokens...";

    // Get client credentials: compile-time define > environment variable
    QString clientId;
    QString clientSecret;

#ifdef CHATTERINO_KICK_CLIENT_ID
    clientId = QStringLiteral(CHATTERINO_KICK_CLIENT_ID);
#endif
    if (clientId.isEmpty())
    {
        clientId = qEnvironmentVariable("CHATTERINO_KICK_CLIENT_ID", "");
    }

#ifdef CHATTERINO_KICK_CLIENT_SECRET
    clientSecret = QStringLiteral(CHATTERINO_KICK_CLIENT_SECRET);
#endif
    if (clientSecret.isEmpty())
    {
        clientSecret =
            qEnvironmentVariable("CHATTERINO_KICK_CLIENT_SECRET", "");
    }

    if (clientId.isEmpty())
    {
        qCWarning(chatterinoKick)
            << "No Kick Client ID configured. Token exchange will fail.";
        this->authenticationFailed.invoke(
            "Missing Kick Client ID - rebuild with credentials or set "
            "environment variable");
        this->cancel();
        return;
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);

    QUrl tokenUrl(KICK_TOKEN_URL);
    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("grant_type", "authorization_code");
    postData.addQueryItem("client_id", clientId);
    if (!clientSecret.isEmpty())
    {
        postData.addQueryItem("client_secret", clientSecret);
    }
    postData.addQueryItem("code", code);
    postData.addQueryItem("redirect_uri", REDIRECT_URI);
    postData.addQueryItem("code_verifier", this->codeVerifier_);

    QNetworkReply *reply =
        manager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    QObject::connect(
        reply, &QNetworkReply::finished, this, [this, reply, manager] {
            reply->deleteLater();
            manager->deleteLater();

            if (reply->error() != QNetworkReply::NoError)
            {
                qCWarning(chatterinoKick)
                    << "Token exchange failed:" << reply->errorString();
                this->authenticationFailed.invoke(
                    QString("Token exchange failed: %1")
                        .arg(reply->errorString()));
                this->cancel();
                return;
            }

            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (!doc.isObject())
            {
                this->authenticationFailed.invoke("Invalid token response");
                this->cancel();
                return;
            }

            QJsonObject obj = doc.object();

            // Check for error response
            if (obj.contains("error"))
            {
                QString error = obj["error"].toString();
                QString description = obj["error_description"].toString();
                this->authenticationFailed.invoke(
                    QString("OAuth error: %1 - %2").arg(error, description));
                this->cancel();
                return;
            }

            // Extract tokens
            Tokens tokens;
            tokens.accessToken = obj["access_token"].toString();
            tokens.refreshToken = obj["refresh_token"].toString();
            tokens.scope = obj["scope"].toString();

            int expiresIn = obj["expires_in"].toInt(3600);
            tokens.expiresAt = QDateTime::currentDateTime().addSecs(expiresIn);

            if (tokens.accessToken.isEmpty())
            {
                this->authenticationFailed.invoke(
                    "No access token in response");
                this->cancel();
                return;
            }

            qCDebug(chatterinoKick) << "Successfully obtained tokens";
            qCDebug(chatterinoKick)
                << "Granted scopes:" << tokens.scope
                << "| Requested: chat:write user:read channel:read";

            // Warn if chat:write scope wasn't granted
            if (!tokens.scope.contains("chat:write"))
            {
                qCWarning(chatterinoKick)
                    << "WARNING: chat:write scope not granted! "
                    << "Ensure this scope is enabled in your Kick developer "
                       "app settings.";
            }
            this->timeoutTimer_.stop();
            this->isInProgress_ = false;
            this->stopLocalServer();
            this->authenticationSuccess.invoke(tokens);
        });
}

}  // namespace chatterino
