#include "providers/twitch/TwitchNameHistory.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

using namespace chatterino;

constexpr int TIMEOUT_MS = 30000;

QHash<QString, TwitchNameHistory> &cache()
{
    static QHash<QString, TwitchNameHistory> instance;
    return instance;
}

QString normalizeLogin(const QString &login)
{
    return login.trimmed().toLower();
}

QDateTime parseTimestamp(const QString &isoTimestamp)
{
    auto timestamp = QDateTime::fromString(isoTimestamp, Qt::ISODateWithMs);
    if (!timestamp.isValid())
    {
        timestamp = QDateTime::fromString(isoTimestamp, Qt::ISODate);
    }
    return timestamp;
}

QString formatDate(const QString &isoTimestamp)
{
    auto timestamp = parseTimestamp(isoTimestamp);
    if (!timestamp.isValid())
    {
        return QStringLiteral("Unknown");
    }

    return timestamp.toLocalTime().date().toString("MMM d, yyyy");
}

}  // namespace

namespace chatterino {

TwitchNameHistory parseTwitchNameHistory(const QJsonArray &root,
                                         const QString &userId,
                                         const QString &requestedLogin)
{
    TwitchNameHistory result;
    result.userId = userId;

    // the login with the newest last_timestamp is the one in use now
    QDateTime newestSeenAt;
    for (const auto &value : root)
    {
        auto login = normalizeLogin(value.toObject()["user_login"].toString());
        if (login.isEmpty())
        {
            continue;
        }

        auto lastSeenAt =
            parseTimestamp(value.toObject()["last_timestamp"].toString());
        if (!newestSeenAt.isValid() ||
            (lastSeenAt.isValid() && lastSeenAt > newestSeenAt))
        {
            newestSeenAt = lastSeenAt;
            result.currentLogin = login;
        }
    }
    if (result.currentLogin.isEmpty())
    {
        result.currentLogin = normalizeLogin(requestedLogin);
    }

    result.entries.reserve(static_cast<size_t>(
        std::min<int>(root.size(), TWITCH_NAME_HISTORY_LIMIT)));

    bool addedCurrent = false;
    for (auto i = root.size() - 1; i >= 0; --i)
    {
        auto object = root.at(i).toObject();
        auto login = normalizeLogin(object["user_login"].toString());
        if (login.isEmpty())
        {
            continue;
        }

        auto firstSeen = formatDate(object["first_timestamp"].toString());

        if (!addedCurrent &&
            login.compare(result.currentLogin, Qt::CaseInsensitive) == 0)
        {
            addedCurrent = true;
            result.entries.push_back(
                {std::move(login), firstSeen, QStringLiteral("Present")});
        }
        else
        {
            // the oldest entry's start is whenever the account was made, which
            // the logs service cannot know
            if (i == 0)
            {
                firstSeen = QStringLiteral("Unknown");
            }
            result.entries.push_back(
                {std::move(login), firstSeen,
                 formatDate(object["last_timestamp"].toString())});
        }

        if (static_cast<int>(result.entries.size()) >=
            TWITCH_NAME_HISTORY_LIMIT)
        {
            break;
        }
    }

    return result;
}

std::optional<TwitchNameHistory> getCachedTwitchNameHistory(
    const QString &userId)
{
    auto it = cache().find(userId.trimmed());
    if (it == cache().end())
    {
        return std::nullopt;
    }

    return *it;
}

void fetchTwitchNameHistory(const QString &userId,
                            const QString &requestedLogin,
                            std::function<void(TwitchNameHistory)> onSuccess,
                            std::function<void(const QString &)> onError)
{
    auto trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
    {
        onError("Missing user id");
        return;
    }

    QUrl url(QStringLiteral("https://logs.zonian.dev/namehistory/") +
             QString::fromUtf8(QUrl::toPercentEncoding(trimmedUserId)));

    NetworkRequest(url)
        .timeout(TIMEOUT_MS)
        .followRedirects(true)
        .header("Accept", "application/json")
        .onSuccess([trimmedUserId, requestedLogin,
                    onSuccess = std::move(onSuccess),
                    onError](const NetworkResult &result) mutable {
            auto root = result.parseJsonValue();
            if (!root.isArray())
            {
                onError("Unexpected name history response");
                return;
            }

            auto history = parseTwitchNameHistory(root.toArray(), trimmedUserId,
                                                  requestedLogin);
            cache().insert(trimmedUserId, history);
            onSuccess(std::move(history));
        })
        .onError([onError = std::move(onError)](const NetworkResult &result) {
            onError(result.formatError());
        })
        .execute();
}

}  // namespace chatterino
