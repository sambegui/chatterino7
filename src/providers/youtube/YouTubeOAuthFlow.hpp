#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QTcpServer;

namespace chatterino {

/// Google OAuth 2.0 loopback flow, used only to obtain a token for *sending*
/// YouTube chat messages.
///
/// The redirect URI is derived from the port the local server actually bound
/// to, rather than being fixed up front: Google's "Desktop app" clients accept
/// any loopback port, and pinning one means the whole flow hangs whenever that
/// port happens to be busy.
class YouTubeOAuthFlow : public QObject
{
    Q_OBJECT

public:
    explicit YouTubeOAuthFlow(QObject *parent = nullptr);
    ~YouTubeOAuthFlow() override;

    /// Binds a loopback port and opens the consent screen in the browser.
    /// Returns false if no port could be bound or the client id is missing.
    bool start();

    /// Stops listening. Safe to call at any point.
    void cancel();

    /// The loopback URI this flow is listening on. Only valid after start().
    [[nodiscard]] const QString &redirectUri() const;

Q_SIGNALS:
    /// The user approved and Google handed back an authorization code.
    void codeReceived(const QString &code, const QString &redirectUri);
    /// The flow failed or the user denied consent.
    void failed(const QString &error);

private:
    void onConnection();
    void respond(class QTcpSocket *socket, const QString &title,
                 const QString &body);

    std::unique_ptr<QTcpServer> server_;
    QString redirectUri_;
    QString state_;
};

}  // namespace chatterino
