#include "util/UrlParser.hpp"

#include <QRegularExpression>
#include <QUrl>

namespace {

/// Kick usernames are lowercase alphanumerics and underscores, up to 25 chars.
bool isValidChannelSlug(const QString &slug)
{
    static const QRegularExpression usernameRegex(
        QStringLiteral("^[a-z0-9_]{1,25}$"));
    return usernameRegex.match(slug).hasMatch();
}

}  // namespace

namespace chatterino {

std::optional<UrlParser::KickChannelResult> UrlParser::parseKickChannel(
    const QString &input)
{
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    // Check if it's a URL
    if (isKickUrl(trimmed))
    {
        // Normalize the URL
        QString normalized = trimmed;
        if (!normalized.startsWith("http://", Qt::CaseInsensitive) &&
            !normalized.startsWith("https://", Qt::CaseInsensitive))
        {
            // a "://" that isn't a scheme we accept is malformed, not something
            // to prefix over
            if (normalized.contains("://"))
            {
                return std::nullopt;
            }
            normalized = "https://" + normalized;
        }

        QUrl url(normalized);
        if (!url.isValid())
        {
            return std::nullopt;
        }

        // Extract path - should be /{username}
        QString path = url.path();
        if (path.isEmpty() || path == "/")
        {
            return std::nullopt;
        }

        // Remove leading slash
        if (path.startsWith('/'))
        {
            path = path.mid(1);
        }

        // Remove any trailing parts (e.g., /xqc/clips -> xqc)
        int slashIndex = path.indexOf('/');
        if (slashIndex > 0)
        {
            path = path.left(slashIndex);
        }

        QString slug = normalizeChannelSlug(path);
        if (!isValidChannelSlug(slug))
        {
            return std::nullopt;
        }

        return KickChannelResult{slug};
    }

    // Not a URL, treat as plain username
    QString slug = normalizeChannelSlug(trimmed);
    if (!isValidChannelSlug(slug))
    {
        return std::nullopt;
    }

    return KickChannelResult{slug};
}

bool UrlParser::isKickUrl(const QString &input)
{
    QString lower = input.toLower();

    // Check for common Kick URL patterns
    return lower.contains("kick.com/") || lower.startsWith("kick.com") ||
           lower.contains("www.kick.com");
}

QString UrlParser::normalizeChannelSlug(const QString &slug)
{
    return slug.trimmed().toLower();
}

}  // namespace chatterino
