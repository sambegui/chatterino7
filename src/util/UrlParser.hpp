#pragma once

#include <QString>

#include <optional>

namespace chatterino {

/// Utility for parsing channel identifiers from URLs and usernames
class UrlParser
{
public:
    /// Result of parsing a Kick channel identifier
    struct KickChannelResult {
        QString channelSlug;  // The extracted channel slug (username)
    };

    /// Parse a Kick channel identifier from a URL or username
    /// Accepts:
    ///   - "xqc" (plain username)
    ///   - "kick.com/xqc"
    ///   - "https://kick.com/xqc"
    ///   - "http://kick.com/xqc"
    ///   - "www.kick.com/xqc"
    /// @param input The input string (URL or username)
    /// @return The parsed channel slug, or std::nullopt if parsing failed
    static std::optional<KickChannelResult> parseKickChannel(
        const QString &input);

    /// Check if a string looks like a Kick URL
    /// @param input The input string
    /// @return true if the string appears to be a Kick URL
    static bool isKickUrl(const QString &input);

    /// Normalize a channel slug (lowercase, trim whitespace)
    /// @param slug The raw channel slug
    /// @return The normalized channel slug
    static QString normalizeChannelSlug(const QString &slug);
};

}  // namespace chatterino
