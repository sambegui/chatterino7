#pragma once

#include <QColor>
#include <QDateTime>
#include <QString>

#include <vector>

namespace chatterino {

/// What a live chat entry actually is. YouTube delivers all of these through
/// the same action list, so the renderer name is the only thing separating a
/// normal message from a $500 superchat.
enum class YouTubeItemKind {
    Text,
    SuperChat,
    SuperSticker,
    NewMember,
    MemberMilestone,
    GiftPurchase,
    GiftRedemption,
    Placeholder,
};

/// One piece of a message body. YouTube splits a message into runs so that
/// custom membership emoji and unicode emoji can carry their own image.
struct YouTubeRun {
    /// Text content. Empty for an emoji run.
    QString text;
    /// Image for an emoji run, empty for a text run.
    QString emojiUrl;
    /// Shortcode such as ":_hi:", used as the emote name and tooltip.
    QString emojiLabel;
    /// True when this run came from a custom (channel membership) emoji rather
    /// than a plain unicode one, which YouTube also ships as an image.
    bool isCustomEmoji{false};

    [[nodiscard]] bool isEmoji() const
    {
        return !this->emojiUrl.isEmpty();
    }
};

/// The author of a live chat message.
struct YouTubeAuthor {
    QString channelId;
    QString name;
    QString avatarUrl;
    bool isOwner{false};
    bool isModerator{false};
    bool isVerified{false};
    /// Channel members get a badge whose image is member-tier specific.
    bool isMember{false};
    QString memberBadgeUrl;
    QString memberBadgeTooltip;
};

/// A single parsed live chat entry.
struct YouTubeMessage {
    QString id;
    QDateTime timestamp;
    YouTubeAuthor author;
    std::vector<YouTubeRun> runs;

    YouTubeItemKind kind{YouTubeItemKind::Text};

    /// Superchat / supersticker only: the localised amount ("$5.00").
    QString purchaseAmount;
    /// Superchat / supersticker only: the tier colour YouTube assigns.
    QColor bodyColor;
    /// Membership events only: the tier or milestone headline.
    QString eventHeadline;

    /// The plain-text form of the body, with emoji rendered as their label.
    [[nodiscard]] QString plainText() const;
};

}  // namespace chatterino
