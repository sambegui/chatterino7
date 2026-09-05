#include "providers/youtube/YouTubeApi.hpp"

#include "common/QLogging.hpp"
#include "providers/youtube/YouTubeAccount.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

using namespace chatterino;

const QString API_BASE = QStringLiteral("https://www.googleapis.com/youtube/v3");
const QString TOKEN_URL = QStringLiteral("https://oauth2.googleapis.com/token");

QString errorFromReply(QNetworkReply *reply, const QByteArray &body)
{
    auto obj = QJsonDocument::fromJson(body).object();
    auto message = obj.value("error")
                       .toObject()
                       .value("message")
                       .toString();
    if (!message.isEmpty())
    {
        return message;
    }
    // The OAuth endpoint uses a flatter shape than the Data API.
    auto oauthError = obj.value("error_description").toString();
    if (!oauthError.isEmpty())
    {
        return oauthError;
    }
    return reply->errorString();
}

}  // namespace

namespace chatterino {

YouTubeApi::YouTubeApi(QObject *parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
}

YouTubeApi::~YouTubeApi() = default;

void YouTubeApi::setAccount(std::shared_ptr<YouTubeAccount> account)
{
    this->account_ = std::move(account);
}

QString YouTubeApi::clientId()
{
    return qEnvironmentVariable("CHATTERINO_YOUTUBE_CLIENT_ID");
}

QString YouTubeApi::clientSecret()
{
    return qEnvironmentVariable("CHATTERINO_YOUTUBE_CLIENT_SECRET");
}

bool YouTubeApi::hasClientCredentials()
{
    return !clientId().isEmpty() && !clientSecret().isEmpty();
}

void YouTubeApi::withFreshToken(
    std::function<void(bool, QString)> onFailure,
    std::function<void(QString)> work)
{
    if (!this->account_ || !this->account_->isAuthenticated())
    {
        onFailure(false, QStringLiteral("Not signed in to YouTube"));
        return;
    }

    if (!this->account_->needsRefresh())
    {
        work(this->account_->getAccessToken());
        return;
    }

    this->refreshToken([this, onFailure, work](bool ok, QString error) {
        if (!ok)
        {
            onFailure(false, error);
            return;
        }
        work(this->account_->getAccessToken());
    });
}

void YouTubeApi::refreshToken(std::function<void(bool, QString)> cb)
{
    if (!this->account_ || this->account_->getRefreshToken().isEmpty())
    {
        cb(false, QStringLiteral("No YouTube refresh token stored"));
        return;
    }

    if (!hasClientCredentials())
    {
        cb(false,
           QStringLiteral("CHATTERINO_YOUTUBE_CLIENT_ID / _SECRET are not set"));
        return;
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId());
    form.addQueryItem(QStringLiteral("client_secret"), clientSecret());
    form.addQueryItem(QStringLiteral("refresh_token"),
                      this->account_->getRefreshToken());
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("refresh_token"));

    QNetworkRequest req{QUrl{TOKEN_URL}};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));

    auto *reply =
        this->network_->post(req, form.toString(QUrl::FullyEncoded).toUtf8());

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        auto body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError)
        {
            cb(false, errorFromReply(reply, body));
            return;
        }

        auto obj = QJsonDocument::fromJson(body).object();
        this->account_->setTokens(obj.value("access_token").toString(),
                                  obj.value("refresh_token").toString(),
                                  obj.value("expires_in").toInt());
        cb(true, {});
    });
}

void YouTubeApi::exchangeCode(const QString &code, const QString &redirectUri,
                              std::function<void(bool, QString)> cb)
{
    if (!this->account_)
    {
        cb(false, QStringLiteral("No YouTube account to sign in to"));
        return;
    }

    if (!hasClientCredentials())
    {
        cb(false,
           QStringLiteral("CHATTERINO_YOUTUBE_CLIENT_ID / _SECRET are not set"));
        return;
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("client_id"), clientId());
    form.addQueryItem(QStringLiteral("client_secret"), clientSecret());
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("authorization_code"));

    QNetworkRequest req{QUrl{TOKEN_URL}};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));

    auto *reply =
        this->network_->post(req, form.toString(QUrl::FullyEncoded).toUtf8());

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        auto body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError)
        {
            cb(false, errorFromReply(reply, body));
            return;
        }

        auto obj = QJsonDocument::fromJson(body).object();
        this->account_->setTokens(obj.value("access_token").toString(),
                                  obj.value("refresh_token").toString(),
                                  obj.value("expires_in").toInt());

        // Fetch the signed-in channel so the UI can show who is posting.
        QUrl url{API_BASE + QStringLiteral("/channels")};
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
        q.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
        url.setQuery(q);

        QNetworkRequest me{url};
        me.setRawHeader("Authorization",
                        "Bearer " + this->account_->getAccessToken().toUtf8());

        auto *meReply = this->network_->get(me);
        QObject::connect(meReply, &QNetworkReply::finished, this,
                         [this, meReply, cb] {
                             meReply->deleteLater();
                             if (meReply->error() == QNetworkReply::NoError)
                             {
                                 auto items =
                                     QJsonDocument::fromJson(meReply->readAll())
                                         .object()
                                         .value("items")
                                         .toArray();
                                 if (!items.isEmpty())
                                 {
                                     auto item = items.first().toObject();
                                     this->account_->setIdentity(
                                         item.value("id").toString(),
                                         item.value("snippet")
                                             .toObject()
                                             .value("title")
                                             .toString());
                                 }
                             }
                             // Identity is cosmetic: the grant itself worked.
                             cb(true, {});
                         });
    });
}

void YouTubeApi::resolveLiveChatId(
    const QString &videoId, std::function<void(QString, QString)> cb)
{
    this->withFreshToken(
        [cb](bool, QString error) {
            cb({}, error);
        },
        [this, videoId, cb](QString token) {
            QUrl url{API_BASE + QStringLiteral("/videos")};
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("part"),
                           QStringLiteral("liveStreamingDetails"));
            q.addQueryItem(QStringLiteral("id"), videoId);
            url.setQuery(q);

            QNetworkRequest req{url};
            req.setRawHeader("Authorization", "Bearer " + token.toUtf8());

            auto *reply = this->network_->get(req);
            QObject::connect(reply, &QNetworkReply::finished, this,
                             [reply, cb] {
                                 reply->deleteLater();
                                 auto body = reply->readAll();

                                 if (reply->error() != QNetworkReply::NoError)
                                 {
                                     cb({}, errorFromReply(reply, body));
                                     return;
                                 }

                                 auto items =
                                     QJsonDocument::fromJson(body)
                                         .object()
                                         .value("items")
                                         .toArray();
                                 if (items.isEmpty())
                                 {
                                     cb({}, QStringLiteral(
                                                "YouTube did not return that "
                                                "video"));
                                     return;
                                 }

                                 auto id = items.first()
                                               .toObject()
                                               .value("liveStreamingDetails")
                                               .toObject()
                                               .value("activeLiveChatId")
                                               .toString();
                                 cb(id,
                                    id.isEmpty()
                                        ? QStringLiteral(
                                              "That stream has no active live "
                                              "chat")
                                        : QString{});
                             });
        });
}

void YouTubeApi::sendMessage(const QString &liveChatId, const QString &text,
                             std::function<void(bool, QString)> cb)
{
    this->withFreshToken(cb, [this, liveChatId, text, cb](QString token) {
        QUrl url{API_BASE + QStringLiteral("/liveChat/messages")};
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
        url.setQuery(q);

        QJsonObject body{
            {"snippet",
             QJsonObject{
                 {"liveChatId", liveChatId},
                 {"type", "textMessageEvent"},
                 {"textMessageDetails",
                  QJsonObject{{"messageText", text}}},
             }},
        };

        QNetworkRequest req{url};
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

        auto *reply = this->network_->post(
            req, QJsonDocument{body}.toJson(QJsonDocument::Compact));

        QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb] {
            reply->deleteLater();
            auto responseBody = reply->readAll();

            if (reply->error() != QNetworkReply::NoError)
            {
                cb(false, errorFromReply(reply, responseBody));
                return;
            }
            cb(true, {});
        });
    });
}

}  // namespace chatterino
