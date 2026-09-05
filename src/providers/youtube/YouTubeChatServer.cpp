#include "providers/youtube/YouTubeChatServer.hpp"

#include "common/QLogging.hpp"
#include "providers/youtube/YouTubeAccount.hpp"
#include "providers/youtube/YouTubeApi.hpp"
#include "providers/youtube/YouTubeChannel.hpp"

namespace chatterino {

YouTubeChatServer::YouTubeChatServer() = default;

YouTubeChatServer::~YouTubeChatServer() = default;

std::shared_ptr<YouTubeChannel> YouTubeChatServer::getOrCreate(
    const QString &target)
{
    auto key = target.trimmed().toLower();

    this->pruneExpired();

    if (auto existing = this->find(target))
    {
        return existing;
    }

    auto channel = std::make_shared<YouTubeChannel>(target.trimmed());
    // Wire sending up front so the channel never has to know how the account
    // is stored; reading works regardless of whether anyone is signed in.
    channel->setAccount(this->account());
    channel->setApi(this->api());
    this->channels_[key] = channel;

    qCDebug(chatterinoYouTube) << "Created YouTube channel for" << target;

    return channel;
}

std::shared_ptr<YouTubeChannel> YouTubeChatServer::find(
    const QString &target) const
{
    auto it = this->channels_.find(target.trimmed().toLower());
    if (it == this->channels_.end())
    {
        return nullptr;
    }
    return it->second.lock();
}

std::shared_ptr<YouTubeAccount> YouTubeChatServer::account()
{
    if (!this->account_)
    {
        this->account_ = std::make_shared<YouTubeAccount>();
        this->account_->load();
    }
    return this->account_;
}

std::shared_ptr<YouTubeApi> YouTubeChatServer::api()
{
    if (!this->api_)
    {
        this->api_ = std::make_shared<YouTubeApi>();
        this->api_->setAccount(this->account());
    }
    return this->api_;
}

bool YouTubeChatServer::isSignedIn()
{
    return this->account()->isAuthenticated();
}

void YouTubeChatServer::signOut()
{
    this->account()->clear();
}

void YouTubeChatServer::pruneExpired()
{
    for (auto it = this->channels_.begin(); it != this->channels_.end();)
    {
        it = it->second.expired() ? this->channels_.erase(it) : std::next(it);
    }
}

}  // namespace chatterino
