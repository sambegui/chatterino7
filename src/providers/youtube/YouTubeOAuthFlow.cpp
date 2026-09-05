#include "providers/youtube/YouTubeOAuthFlow.hpp"

#include "common/QLogging.hpp"
#include "providers/youtube/YouTubeApi.hpp"

#include <QDesktopServices>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {

const QString AUTH_URL =
    QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");

/// Posting a live chat message needs the full-control scope; there is no
/// narrower one that permits liveChatMessages.insert.
const QString SCOPE =
    QStringLiteral("https://www.googleapis.com/auth/youtube.force-ssl");

}  // namespace

namespace chatterino {

YouTubeOAuthFlow::YouTubeOAuthFlow(QObject *parent)
    : QObject(parent)
{
}

YouTubeOAuthFlow::~YouTubeOAuthFlow()
{
    this->cancel();
}

const QString &YouTubeOAuthFlow::redirectUri() const
{
    return this->redirectUri_;
}

bool YouTubeOAuthFlow::start()
{
    if (YouTubeApi::clientId().isEmpty())
    {
        Q_EMIT this->failed(
            QStringLiteral("CHATTERINO_YOUTUBE_CLIENT_ID is not set"));
        return false;
    }

    this->server_ = std::make_unique<QTcpServer>(this);
    QObject::connect(this->server_.get(), &QTcpServer::newConnection, this,
                     &YouTubeOAuthFlow::onConnection);

    // Port 0 asks the OS for any free port, then the redirect URI is built from
    // whatever we actually got.
    if (!this->server_->listen(QHostAddress::LocalHost, 0))
    {
        Q_EMIT this->failed(
            QStringLiteral("Could not open a local port for the YouTube login"));
        this->server_.reset();
        return false;
    }

    this->redirectUri_ = QStringLiteral("http://127.0.0.1:%1")
                             .arg(this->server_->serverPort());
    this->state_ = QString::number(QRandomGenerator::global()->generate64(), 16);

    QUrl url{AUTH_URL};
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), YouTubeApi::clientId());
    query.addQueryItem(QStringLiteral("redirect_uri"), this->redirectUri_);
    query.addQueryItem(QStringLiteral("response_type"),
                       QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), SCOPE);
    // offline + consent is what makes Google return a refresh token, without
    // which the login would silently stop working after an hour.
    query.addQueryItem(QStringLiteral("access_type"),
                       QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("state"), this->state_);
    url.setQuery(query);

    qCDebug(chatterinoYouTube)
        << "YouTube OAuth listening on" << this->redirectUri_;

    QDesktopServices::openUrl(url);
    return true;
}

void YouTubeOAuthFlow::cancel()
{
    if (this->server_)
    {
        this->server_->close();
        this->server_.reset();
    }
}

void YouTubeOAuthFlow::onConnection()
{
    auto *socket = this->server_->nextPendingConnection();
    if (socket == nullptr)
    {
        return;
    }

    QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        auto request = QString::fromUtf8(socket->readAll());
        // "GET /?code=... HTTP/1.1"
        auto firstLine = request.section(QStringLiteral("\r\n"), 0, 0);
        auto target = firstLine.section(' ', 1, 1);

        QUrlQuery query{QUrl{QStringLiteral("http://127.0.0.1") + target}
                            .query()};

        auto error = query.queryItemValue(QStringLiteral("error"));
        if (!error.isEmpty())
        {
            this->respond(socket, QStringLiteral("Login cancelled"),
                          QStringLiteral("You can close this tab."));
            Q_EMIT this->failed(error);
            this->cancel();
            return;
        }

        auto state = query.queryItemValue(QStringLiteral("state"));
        auto code = query.queryItemValue(QStringLiteral("code"));

        if (code.isEmpty())
        {
            // Browsers also request /favicon.ico on this port; ignore anything
            // that is not the callback rather than failing the flow.
            socket->close();
            socket->deleteLater();
            return;
        }

        if (state != this->state_)
        {
            this->respond(socket, QStringLiteral("Login failed"),
                          QStringLiteral("The login response did not match "
                                         "this request."));
            Q_EMIT this->failed(
                QStringLiteral("OAuth state mismatch, login rejected"));
            this->cancel();
            return;
        }

        this->respond(socket, QStringLiteral("Signed in to YouTube"),
                      QStringLiteral("You can close this tab and go back to "
                                     "Chatterino."));
        Q_EMIT this->codeReceived(code, this->redirectUri_);
        this->cancel();
    });

    QObject::connect(socket, &QTcpSocket::disconnected, socket,
                     &QTcpSocket::deleteLater);
}

void YouTubeOAuthFlow::respond(QTcpSocket *socket, const QString &title,
                               const QString &body)
{
    auto html = QStringLiteral(
                    "<!doctype html><meta charset=\"utf-8\">"
                    "<title>%1</title>"
                    "<body style=\"font-family:system-ui;background:#18181b;"
                    "color:#efeff1;display:grid;place-items:center;"
                    "height:100vh;margin:0\">"
                    "<div style=\"text-align:center\"><h2>%1</h2><p>%2</p>"
                    "</div></body>")
                    .arg(title, body);

    auto payload = html.toUtf8();
    QByteArray response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Content-Length: " +
                          QByteArray::number(payload.size()) +
                          "\r\n"
                          "Connection: close\r\n\r\n" +
                          payload;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

}  // namespace chatterino
