#include "providers/youtube/YouTubeAccount.hpp"

#include <QSettings>

namespace {

const QString KEY_ACCESS_TOKEN = QStringLiteral("youtube/accessToken");
const QString KEY_REFRESH_TOKEN = QStringLiteral("youtube/refreshToken");
const QString KEY_EXPIRES_AT = QStringLiteral("youtube/expiresAt");
const QString KEY_CHANNEL_ID = QStringLiteral("youtube/channelId");
const QString KEY_DISPLAY_NAME = QStringLiteral("youtube/displayName");

/// Refresh a little early so a send never races the expiry.
constexpr int EXPIRY_MARGIN_SECONDS = 60;

}  // namespace

namespace chatterino {

bool YouTubeAccount::isAuthenticated() const
{
    return !this->accessToken_.isEmpty() || !this->refreshToken_.isEmpty();
}

bool YouTubeAccount::needsRefresh() const
{
    if (this->accessToken_.isEmpty())
    {
        return true;
    }
    if (!this->expiresAt_.isValid())
    {
        return false;
    }
    return QDateTime::currentDateTime().secsTo(this->expiresAt_) <
           EXPIRY_MARGIN_SECONDS;
}

const QString &YouTubeAccount::getAccessToken() const
{
    return this->accessToken_;
}

const QString &YouTubeAccount::getRefreshToken() const
{
    return this->refreshToken_;
}

const QString &YouTubeAccount::getChannelId() const
{
    return this->channelId_;
}

const QString &YouTubeAccount::getDisplayName() const
{
    return this->displayName_;
}

void YouTubeAccount::setTokens(const QString &accessToken,
                               const QString &refreshToken,
                               int expiresInSeconds)
{
    this->accessToken_ = accessToken;
    // Google only returns a refresh token on the first consent, so an empty one
    // in a refresh response must not wipe the stored one.
    if (!refreshToken.isEmpty())
    {
        this->refreshToken_ = refreshToken;
    }
    this->expiresAt_ =
        QDateTime::currentDateTime().addSecs(std::max(0, expiresInSeconds));
    this->save();
}

void YouTubeAccount::setIdentity(const QString &channelId,
                                 const QString &displayName)
{
    this->channelId_ = channelId;
    this->displayName_ = displayName;
    this->save();
}

void YouTubeAccount::clear()
{
    this->accessToken_.clear();
    this->refreshToken_.clear();
    this->channelId_.clear();
    this->displayName_.clear();
    this->expiresAt_ = {};

    QSettings settings;
    settings.remove(KEY_ACCESS_TOKEN);
    settings.remove(KEY_REFRESH_TOKEN);
    settings.remove(KEY_EXPIRES_AT);
    settings.remove(KEY_CHANNEL_ID);
    settings.remove(KEY_DISPLAY_NAME);
}

void YouTubeAccount::save() const
{
    QSettings settings;
    settings.setValue(KEY_ACCESS_TOKEN, this->accessToken_);
    settings.setValue(KEY_REFRESH_TOKEN, this->refreshToken_);
    settings.setValue(KEY_EXPIRES_AT, this->expiresAt_.toString(Qt::ISODate));
    settings.setValue(KEY_CHANNEL_ID, this->channelId_);
    settings.setValue(KEY_DISPLAY_NAME, this->displayName_);
}

void YouTubeAccount::load()
{
    QSettings settings;
    this->accessToken_ = settings.value(KEY_ACCESS_TOKEN).toString();
    this->refreshToken_ = settings.value(KEY_REFRESH_TOKEN).toString();
    this->channelId_ = settings.value(KEY_CHANNEL_ID).toString();
    this->displayName_ = settings.value(KEY_DISPLAY_NAME).toString();
    this->expiresAt_ = QDateTime::fromString(
        settings.value(KEY_EXPIRES_AT).toString(), Qt::ISODate);
}

}  // namespace chatterino
