#pragma once

#include "providers/kick/KickMessage.hpp"

#include <pajlada/signals/signal.hpp>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWebSocket>

#include <memory>

namespace chatterino {

/// Kick chat WebSocket connection using Pusher protocol
/// Handles connection, subscription, and message parsing for Kick.com chat
class KickWebSocket : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KickWebSocket)

public:
    explicit KickWebSocket(QObject *parent = nullptr);
    ~KickWebSocket() override;

    /// Connect to Kick WebSocket endpoint
    /// @return true if connection initiated successfully
    bool connect();

    /// Disconnect from WebSocket
    void disconnect();

    /// Subscribe to a channel's chat messages
    /// @param chatroomId The chatroom ID to subscribe to
    void subscribe(int chatroomId);

    /// Unsubscribe from a channel's chat messages
    /// @param chatroomId The chatroom ID to unsubscribe from
    void unsubscribe(int chatroomId);

    /// Check if connected to WebSocket
    [[nodiscard]] bool isConnected() const;

    /// Signal emitted when a chat message is received
    pajlada::Signals::Signal<KickMessage> messageReceived;

    /// Signal emitted when connection state changes
    pajlada::Signals::Signal<bool> connectionStateChanged;

    /// Signal emitted on error
    pajlada::Signals::Signal<QString> errorOccurred;

private Q_SLOTS:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(QAbstractSocket::SocketError error);
    void onPingTimeout();

private:
    /// Parse incoming Pusher protocol message
    void parseMessage(const QString &rawMessage);

    /// Handle Pusher connection established event
    void handleConnectionEstablished(const QJsonObject &data);

    /// Handle Pusher subscription succeeded event
    void handleSubscriptionSucceeded(const QString &channel,
                                     const QJsonObject &data);

    /// Handle chat message event from Kick
    void handleChatMessageEvent(const QJsonObject &data);

    /// Handle Pusher error event
    void handleError(const QJsonObject &data);

    /// Send Pusher ping to keep connection alive
    void sendPing();

    /// Send Pusher protocol message
    void sendPusherMessage(const QString &event, const QJsonObject &data = {});

    /// Pusher WebSocket endpoint for Kick
    static constexpr const char *KICK_PUSHER_ENDPOINT =
        "wss://ws-us2.pusher.com/app/"
        "32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0-rc2&flash="
        "false";

    /// Pusher ping interval (30 seconds)
    static constexpr int PING_INTERVAL_MS = 30000;

    std::unique_ptr<QWebSocket> socket_;
    std::unique_ptr<QTimer> pingTimer_;
    QString socketId_;
    bool isConnected_{false};
};

}  // namespace chatterino
