#include "providers/kick/KickEmotes.hpp"

#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

// Base emote size matching Twitch's system (28x28 pixels)
constexpr QSize KICK_EMOTE_BASE_SIZE(28, 28);

}  // namespace

namespace chatterino {

KickEmotes::KickEmotes()
    : globalEmotes_(std::make_shared<EmoteMap>())
{
}

EmotePtr KickEmotes::getEmote(const QString &emoteName) const
{
    // Check global emotes first
    if (this->globalEmotes_)
    {
        auto it = this->globalEmotes_->find(EmoteName{emoteName});
        if (it != this->globalEmotes_->end())
        {
            return it->second;
        }
    }
    return nullptr;
}

void KickEmotes::loadGlobalEmotes(std::function<void(bool success)> callback)
{
    // Global Kick emotes are available at https://kick.com/emotes/global
    // Response: [{ ..., "emotes": [{ "id": int, "name": string, "subscribers_only": bool }] }]

    qCDebug(chatterinoKick) << "Loading global Kick emotes...";

    auto *manager = new QNetworkAccessManager();
    QNetworkRequest request{QUrl{"https://kick.com/emotes/global"}};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                      "AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager->get(request);

    QObject::connect(
        reply, &QNetworkReply::finished, [this, reply, manager, callback]() {
            reply->deleteLater();
            manager->deleteLater();

            if (reply->error() != QNetworkReply::NoError)
            {
                qCWarning(chatterinoKick)
                    << "Failed to load global Kick emotes:"
                    << reply->errorString();
                if (callback)
                {
                    callback(false);
                }
                return;
            }

            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);

            if (!doc.isArray())
            {
                qCWarning(chatterinoKick)
                    << "Invalid global emotes response - expected array";
                if (callback)
                {
                    callback(false);
                }
                return;
            }

            auto newEmotes = std::make_shared<EmoteMap>();
            QJsonArray channelArray = doc.array();

            // Response is array of channel objects, each with "emotes" array
            for (const auto &channelVal : channelArray)
            {
                QJsonObject channelObj = channelVal.toObject();
                QJsonArray emotesArray = channelObj["emotes"].toArray();

                for (const auto &emoteVal : emotesArray)
                {
                    QJsonObject emoteObj = emoteVal.toObject();
                    int id = emoteObj["id"].toInt();
                    QString name = emoteObj["name"].toString();
                    bool subscribersOnly =
                        emoteObj["subscribers_only"].toBool();

                    // Skip subscriber-only global emotes
                    if (subscribersOnly)
                    {
                        continue;
                    }

                    if (name.isEmpty() || id == 0)
                    {
                        continue;
                    }

                    // Create emote URL
                    QString url =
                        QString("https://files.kick.com/emotes/%1/fullsize")
                            .arg(id);

                    // Create emote
                    auto emote = std::make_shared<Emote>(Emote{
                        EmoteName{name},
                        ImageSet{
                            Image::fromUrl({url}, 1.0, KICK_EMOTE_BASE_SIZE)},
                        Tooltip{QString("%1<br>Global Kick Emote").arg(name)},
                        Url{url},
                        false,  // Not zero-width
                        EmoteId{QString::number(id)},
                    });

                    newEmotes->emplace(EmoteName{name}, emote);
                }
            }

            this->globalEmotes_ = newEmotes;
            qCDebug(chatterinoKick)
                << "Loaded" << newEmotes->size() << "global Kick emotes";

            if (callback)
            {
                callback(true);
            }
        });
}

void KickEmotes::loadChannelEmotes(const QString &channelSlug,
                                   std::function<void(bool success)> callback)
{
    // TODO: Load channel-specific Kick emotes when API is available
    // For now, 7TV channel emotes work automatically through existing providers

    qCDebug(chatterinoKick)
        << "Kick channel emotes for" << channelSlug
        << ": using 7TV/BTTV/FFZ providers for emote support";

    // Create empty emote map for this channel
    this->channelEmotes_[channelSlug] = std::make_shared<EmoteMap>();

    if (callback)
    {
        callback(true);
    }
}

const EmoteMap &KickEmotes::getGlobalEmotes() const
{
    static EmoteMap empty;
    return this->globalEmotes_ ? *this->globalEmotes_ : empty;
}

std::shared_ptr<const EmoteMap> KickEmotes::globalEmotes() const
{
    return this->globalEmotes_;
}

std::shared_ptr<const EmoteMap> KickEmotes::getChannelEmotes(
    const QString &channelSlug) const
{
    auto it = this->channelEmotes_.find(channelSlug);
    if (it != this->channelEmotes_.end())
    {
        return it->second;
    }
    return nullptr;
}

void KickEmotes::clearCache()
{
    this->globalEmotes_ = std::make_shared<EmoteMap>();
    this->channelEmotes_.clear();
}

void KickEmotes::parseEmoteData(const QByteArray &data, EmoteMap &emoteMap)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qCWarning(chatterinoKick)
            << "Failed to parse emote data:" << parseError.errorString();
        return;
    }

    if (!doc.isArray())
    {
        qCWarning(chatterinoKick) << "Emote data is not an array";
        return;
    }

    QJsonArray emoteArray = doc.array();
    for (const auto &emoteVal : emoteArray)
    {
        QJsonObject emoteObj = emoteVal.toObject();

        QString name = emoteObj["name"].toString();
        QString id = emoteObj["id"].toString();
        QString url = emoteObj["url"].toString();

        if (name.isEmpty() || url.isEmpty())
        {
            continue;
        }

        // Create emote
        EmoteId emoteId{id};
        EmoteName emoteName{name};

        // Create ImageSet with single image - Kick provides one high-res image
        // The system will scale it as needed for different display densities
        auto emote = std::make_shared<Emote>(Emote{
            emoteName,
            ImageSet{Image::fromUrl({url}, 1.0, KICK_EMOTE_BASE_SIZE)},
            Tooltip{name + "<br>Kick Emote"},
            Url{url},
            false,  // Not zero-width
            emoteId,
        });

        emoteMap[emoteName] = emote;
    }

    qCDebug(chatterinoKick) << "Loaded" << emoteMap.size() << "Kick emotes";
}

}  // namespace chatterino
