#pragma once

#include <QJsonArray>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace chatterino {

constexpr int TWITCH_NAME_HISTORY_LIMIT = 50;

struct TwitchNameHistoryEntry {
    QString login;
    /// Formatted date, or "Unknown" when the source has no timestamp.
    QString firstSeen;
    /// Formatted date, or "Present" for the login the user has now.
    QString lastSeen;
};

struct TwitchNameHistory {
    QString userId;
    QString currentLogin;
    /// Newest first, capped at TWITCH_NAME_HISTORY_LIMIT.
    std::vector<TwitchNameHistoryEntry> entries;
};

/// Parses the logs service's response: one object per login the user has held,
/// oldest first, each carrying the window that login was seen in.
TwitchNameHistory parseTwitchNameHistory(const QJsonArray &root,
                                         const QString &userId,
                                         const QString &requestedLogin);

/// Returns a history fetched earlier this session, if one is cached for this
/// user id.
std::optional<TwitchNameHistory> getCachedTwitchNameHistory(
    const QString &userId);

/// Name history is not a Twitch API. This queries a third-party logs service,
/// so treat a failure as "unavailable" rather than an error worth surfacing
/// loudly.
void fetchTwitchNameHistory(const QString &userId,
                            const QString &requestedLogin,
                            std::function<void(TwitchNameHistory)> onSuccess,
                            std::function<void(const QString &)> onError);

}  // namespace chatterino
