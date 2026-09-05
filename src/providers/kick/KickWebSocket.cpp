#include "providers/kick/KickWebSocket.hpp"

#include "common/QLogging.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace chatterino {

KickWebSocket::KickWebSocket(QObject *parent)
    : QObject(parent)
    , socket_(std::make_unique<QWebSocket>())
    , pingTimer_(std::make_unique<QTimer>(this))
{
    // Connect WebSocket signals
    QObject::connect(this->socket_.get(), &QWebSocket::connected, this,
                     &KickWebSocket::onConnected);
    QObject::connect(this->socket_.get(), &QWebSocket::disconnected, this,
                     &KickWebSocket::onDisconnected);
    QObject::connect(this->socket_.get(), &QWebSocket::textMessageReceived,
                     this, &KickWebSocket::onTextMessageReceived);
    QObject::connect(
        this->socket_.get(),
        QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
        &KickWebSocket::onError);

    // Connect ping timer
    QObject::connect(this->pingTimer_.get(), &QTimer::timeout, this,
                     &KickWebSocket::onPingTimeout);
}

KickWebSocket::~KickWebSocket()
{
    this->disconnect();
}

bool KickWebSocket::connect()
{
    if (this->isConnected_)
    {
        return true;
    }

    qCDebug(chatterinoKick) << "Connecting to Kick WebSocket...";
    this->socket_->open(QUrl(QString::fromLatin1(KICK_PUSHER_ENDPOINT)));
    return true;
}

void KickWebSocket::disconnect()
{
    this->pingTimer_->stop();
    if (this->socket_->state() != QAbstractSocket::UnconnectedState)
    {
        this->socket_->close();
    }
    this->isConnected_ = false;
    this->socketId_.clear();
}

void KickWebSocket::subscribe(int chatroomId)
{
    if (!this->isConnected_)
    {
        qCWarning(chatterinoKick)
            << "Cannot subscribe: not connected to WebSocket";
        return;
    }

    QString channelName = QString("chatrooms.%1.v2").arg(chatroomId);

    QJsonObject subscribeData;
    subscribeData["channel"] = channelName;

    this->sendPusherMessage("pusher:subscribe", subscribeData);

    qCDebug(chatterinoKick) << "Subscribed to channel:" << channelName;
}

void KickWebSocket::unsubscribe(int chatroomId)
{
    if (!this->isConnected_)
    {
        return;
    }

    QString channelName = QString("chatrooms.%1.v2").arg(chatroomId);

    QJsonObject unsubscribeData;
    unsubscribeData["channel"] = channelName;

    this->sendPusherMessage("pusher:unsubscribe", unsubscribeData);

    qCDebug(chatterinoKick) << "Unsubscribed from channel:" << channelName;
}

bool KickWebSocket::isConnected() const
{
    return this->isConnected_;
}

void KickWebSocket::onConnected()
{
    qCDebug(chatterinoKick) << "WebSocket connected, waiting for Pusher "
                               "connection_established event...";
}

void KickWebSocket::onDisconnected()
{
    qCDebug(chatterinoKick) << "WebSocket disconnected";
    this->pingTimer_->stop();
    this->isConnected_ = false;
    this->socketId_.clear();
    this->connectionStateChanged.invoke(false);
}

void KickWebSocket::onTextMessageReceived(const QString &message)
{
    this->parseMessage(message);
}

void KickWebSocket::onError(QAbstractSocket::SocketError error)
{
    QString errorMsg =
        QString("WebSocket error: %1").arg(this->socket_->errorString());
    qCWarning(chatterinoKick) << errorMsg;
    this->errorOccurred.invoke(errorMsg);
}

void KickWebSocket::onPingTimeout()
{
    this->sendPing();
}

void KickWebSocket::parseMessage(const QString &rawMessage)
{
    QJsonParseError parseError;
    QJsonDocument doc =
        QJsonDocument::fromJson(rawMessage.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qCWarning(chatterinoKick)
            << "Failed to parse WebSocket message:" << parseError.errorString();
        return;
    }

    if (!doc.isObject())
    {
        qCWarning(chatterinoKick) << "WebSocket message is not a JSON object";
        return;
    }

    QJsonObject obj = doc.object();
    QString event = obj["event"].toString();

    // Handle Pusher protocol events
    if (event == "pusher:connection_established")
    {
        QString dataStr = obj["data"].toString();
        QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
        this->handleConnectionEstablished(dataDoc.object());
    }
    else if (event == "pusher_internal:subscription_succeeded")
    {
        QString channel = obj["channel"].toString();
        QString dataStr = obj["data"].toString();
        QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
        this->handleSubscriptionSucceeded(channel, dataDoc.object());
    }
    else if (event == "pusher:pong")
    {
        // Pong received, connection is alive
        qCDebug(chatterinoKick) << "Received pong from server";
    }
    else if (event == "pusher:error")
    {
        QString dataStr = obj["data"].toString();
        QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
        this->handleError(dataDoc.object());
    }
    else if (event == "App\\Events\\ChatMessageEvent")
    {
        // Chat message event - parse the nested JSON
        QString dataStr = obj["data"].toString();
        QJsonDocument dataDoc = QJsonDocument::fromJson(dataStr.toUtf8());
        this->handleChatMessageEvent(dataDoc.object());
    }
    else if (event == "App\\Events\\ChatMessageDeletedEvent" ||
             event == "App\\Events\\UserBannedEvent" ||
             event == "App\\Events\\StreamHostEvent")
    {
        // TODO: Handle moderation events in future
        qCDebug(chatterinoKick) << "Received event (unhandled):" << event;
    }
    else
    {
        qCDebug(chatterinoKick) << "Unknown event:" << event;
    }
}

void KickWebSocket::handleConnectionEstablished(const QJsonObject &data)
{
    this->socketId_ = data["socket_id"].toString();
    this->isConnected_ = true;

    // Start ping timer
    this->pingTimer_->start(PING_INTERVAL_MS);

    qCDebug(chatterinoKick)
        << "Pusher connection established, socket_id:" << this->socketId_;

    this->connectionStateChanged.invoke(true);
}

void KickWebSocket::handleSubscriptionSucceeded(const QString &channel,
                                                const QJsonObject &data)
{
    Q_UNUSED(data);
    qCDebug(chatterinoKick) << "Subscription succeeded for channel:" << channel;
}

void KickWebSocket::handleChatMessageEvent(const QJsonObject &data)
{
    KickMessage message = KickMessage::fromJson(data);
    this->messageReceived.invoke(message);
}

void KickWebSocket::handleError(const QJsonObject &data)
{
    QString message = data["message"].toString();
    int code = data["code"].toInt();

    QString errorMsg = QString("Pusher error %1: %2").arg(code).arg(message);
    qCWarning(chatterinoKick) << errorMsg;
    this->errorOccurred.invoke(errorMsg);

    // Handle specific error codes
    if (code == 4004)
    {
        // Connection limit exceeded - need to wait before reconnecting
        qCWarning(chatterinoKick) << "Connection limit exceeded, waiting...";
    }
    else if (code == 4100 || code == 4200 || code == 4300)
    {
        // Various reconnectable errors
        this->disconnect();
    }
}

void KickWebSocket::sendPing()
{
    if (!this->isConnected_)
    {
        return;
    }

    this->sendPusherMessage("pusher:ping");
    qCDebug(chatterinoKick) << "Sent ping to server";
}

void KickWebSocket::sendPusherMessage(const QString &event,
                                      const QJsonObject &data)
{
    QJsonObject message;
    message["event"] = event;
    if (!data.isEmpty())
    {
        message["data"] = data;
    }

    QJsonDocument doc(message);
    this->socket_->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

}  // namespace chatterino
