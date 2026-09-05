#include "providers/kick/KickChatServer.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickMessage.hpp"
#include "providers/kick/KickWebSocket.hpp"

#include <tuple>

namespace chatterino {

KickChatServer::KickChatServer() = default;
KickChatServer::~KickChatServer() = default;

std::shared_ptr<KickChannel> KickChatServer::getOrCreate(const QString &slug)
{
    auto normalized = slug.toLower();

    if (auto existing = this->findBySlug(normalized))
    {
        return existing;
    }

    auto channel = std::make_shared<KickChannel>(normalized);
    this->bySlug_[normalized] = channel;

    channel->connect();

    return channel;
}

std::shared_ptr<KickChannel> KickChatServer::findBySlug(
    const QString &slug) const
{
    auto it = this->bySlug_.find(slug.toLower());
    if (it == this->bySlug_.end())
    {
        return nullptr;
    }

    return it->second.lock();
}

void KickChatServer::ensureConnected()
{
    if (!this->socket_)
    {
        this->socket_ = std::make_unique<KickWebSocket>();

        std::ignore = this->socket_->messageReceived.connect(
            [this](const KickMessage &message) {
                this->onMessage(message);
            });
        std::ignore = this->socket_->connectionStateChanged.connect(
            [this](bool connected) {
                this->onConnectionStateChanged(connected);
            });
        std::ignore =
            this->socket_->errorOccurred.connect([this](const QString &error) {
                this->onError(error);
            });
    }

    if (!this->socket_->isConnected())
    {
        this->socket_->connect();
    }
}

void KickChatServer::subscribeChatroom(
    int chatroomId, const std::shared_ptr<KickChannel> &channel)
{
    if (chatroomId == 0 || !channel)
    {
        return;
    }

    this->byChatroom_[chatroomId] = channel;

    if (this->socket_)
    {
        this->socket_->subscribe(chatroomId);
    }
}

void KickChatServer::leave(KickChannel *channel)
{
    if (channel == nullptr)
    {
        return;
    }

    auto chatroomId = channel->getChatroomId();
    if (chatroomId != 0)
    {
        auto it = this->byChatroom_.find(chatroomId);
        // only unsubscribe if this really is the channel holding the room
        if (it != this->byChatroom_.end() && it->second.lock().get() == channel)
        {
            if (this->socket_)
            {
                this->socket_->unsubscribe(chatroomId);
            }
            this->byChatroom_.erase(it);
        }
    }

    auto it = this->bySlug_.find(channel->getChannelSlug().toLower());
    if (it != this->bySlug_.end() && it->second.lock().get() == channel)
    {
        this->bySlug_.erase(it);
    }

    // nothing left to listen to, so drop the connection rather than idle on it
    if (this->byChatroom_.empty() && this->socket_)
    {
        this->socket_->disconnect();
    }
}

bool KickChatServer::isConnected() const
{
    return this->socket_ && this->socket_->isConnected();
}

void KickChatServer::onMessage(const KickMessage &message)
{
    auto it = this->byChatroom_.find(message.chatroomId);
    if (it == this->byChatroom_.end())
    {
        return;
    }

    if (auto channel = it->second.lock())
    {
        channel->onMessageReceived(message);
        return;
    }

    // the channel went away, stop paying for its room
    if (this->socket_)
    {
        this->socket_->unsubscribe(message.chatroomId);
    }
    this->byChatroom_.erase(it);
}

void KickChatServer::onConnectionStateChanged(bool connected)
{
    this->pruneExpired();

    if (connected)
    {
        // a fresh socket has no subscriptions, so every channel re-announces
        for (auto &[slug, weak] : this->bySlug_)
        {
            if (auto channel = weak.lock())
            {
                channel->onServerConnected();
            }
        }
        return;
    }

    for (auto &[slug, weak] : this->bySlug_)
    {
        if (auto channel = weak.lock())
        {
            channel->onServerDisconnected();
        }
    }
}

void KickChatServer::onError(const QString &error)
{
    qCWarning(chatterinoKick) << "Kick chat connection error:" << error;

    this->pruneExpired();
    for (auto &[slug, weak] : this->bySlug_)
    {
        if (auto channel = weak.lock())
        {
            channel->onServerError();
        }
    }
}

void KickChatServer::pruneExpired()
{
    std::erase_if(this->bySlug_, [](const auto &entry) {
        return entry.second.expired();
    });
    std::erase_if(this->byChatroom_, [](const auto &entry) {
        return entry.second.expired();
    });
}

}  // namespace chatterino
