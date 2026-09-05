#include "providers/youtube/YouTubeInnerTube.hpp"

#include "common/QLogging.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <optional>

namespace {

using namespace chatterino;

const QString INNERTUBE_CHAT_URL =
    QStringLiteral("https://www.youtube.com/youtubei/v1/live_chat/"
                   "get_live_chat?key=%1&prettyPrint=false");

/// YouTube serves a stub page to anything that does not look like a browser,
/// so this is load-bearing rather than cosmetic.
const QString BROWSER_UA =
    QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                   "AppleWebKit/537.36 (KHTML, like Gecko) "
                   "Chrome/131.0.0.0 Safari/537.36");

QNetworkRequest pageRequest(const QString &url)
{
    QNetworkRequest req{QUrl{url}};
    req.setHeader(QNetworkRequest::UserAgentHeader, BROWSER_UA);
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

/// Pulls the JSON object that follows @a marker out of a YouTube HTML page.
///
/// The payload is embedded in a <script> assignment, so it cannot be found by
/// regex: it contains braces and quotes inside strings. This walks the text
/// tracking string and escape state and stops on the matching close brace.
QJsonObject extractJsonBlob(const QString &html, const QString &marker)
{
    int at = html.indexOf(marker);
    if (at < 0)
    {
        return {};
    }

    int start = html.indexOf('{', at);
    if (start < 0)
    {
        return {};
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (int i = start; i < html.size(); i++)
    {
        QChar c = html[i];

        if (inString)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }

        if (c == '"')
        {
            inString = true;
        }
        else if (c == '{')
        {
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0)
            {
                auto slice = html.mid(start, i - start + 1).toUtf8();
                return QJsonDocument::fromJson(slice).object();
            }
        }
    }

    return {};
}

QString matchFirst(const QString &html, const QString &pattern)
{
    QRegularExpression re{pattern};
    auto m = re.match(html);
    return m.hasMatch() ? m.captured(1) : QString{};
}

/// Collects every "<something>ContinuationData" object in the tree, keyed by
/// its kind. The tokens are scattered: the live tail sits under the chat
/// renderer while the reload tokens hang off the "Top chat / Live chat" view
/// selector, so there is no single path worth hardcoding.
void collectContinuations(const QJsonValue &value,
                          QMap<QString, QList<QJsonObject>> &out)
{
    if (value.isObject())
    {
        auto obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (it.key().endsWith(QStringLiteral("ContinuationData")) &&
                it.value().isObject() &&
                it.value().toObject().contains(QStringLiteral("continuation")))
            {
                out[it.key()].append(it.value().toObject());
            }
            collectContinuations(it.value(), out);
        }
    }
    else if (value.isArray())
    {
        for (const auto &item : value.toArray())
        {
            collectContinuations(item, out);
        }
    }
}

QString largestThumbnail(const QJsonObject &holder)
{
    auto thumbs = holder.value("thumbnails").toArray();
    if (thumbs.isEmpty())
    {
        return {};
    }
    return thumbs.last().toObject().value("url").toString();
}

/// YouTube stores colours as an unsigned 32-bit ARGB integer.
QColor colorFromArgb(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return {};
    }
    auto raw = static_cast<quint32>(value.toDouble());
    return QColor::fromRgb(static_cast<int>((raw >> 16) & 0xFF),
                           static_cast<int>((raw >> 8) & 0xFF),
                           static_cast<int>(raw & 0xFF),
                           static_cast<int>((raw >> 24) & 0xFF));
}

std::vector<YouTubeRun> parseRuns(const QJsonObject &messageObj)
{
    std::vector<YouTubeRun> runs;

    for (const auto &runValue : messageObj.value("runs").toArray())
    {
        auto run = runValue.toObject();

        if (run.contains("emoji"))
        {
            auto emoji = run.value("emoji").toObject();
            YouTubeRun out;
            out.emojiUrl = largestThumbnail(emoji.value("image").toObject());
            out.isCustomEmoji = emoji.value("isCustomEmoji").toBool();

            auto shortcuts = emoji.value("shortcuts").toArray();
            out.emojiLabel = shortcuts.isEmpty()
                                 ? emoji.value("emojiId").toString()
                                 : shortcuts.first().toString();

            // A unicode emoji carries the character itself as its id, which
            // renders far better than a downloaded PNG.
            if (!out.isCustomEmoji)
            {
                auto literal = emoji.value("emojiId").toString();
                if (!literal.isEmpty() && literal.size() <= 8)
                {
                    out.text = literal;
                    out.emojiUrl.clear();
                }
            }

            if (out.emojiUrl.isEmpty() && out.text.isEmpty())
            {
                out.text = out.emojiLabel;
            }
            runs.push_back(std::move(out));
            continue;
        }

        auto text = run.value("text").toString();
        if (!text.isEmpty())
        {
            YouTubeRun out;
            out.text = text;
            runs.push_back(std::move(out));
        }
    }

    return runs;
}

QString runsToText(const QJsonObject &holder)
{
    QString out;
    for (const auto &runValue : holder.value("runs").toArray())
    {
        out += runValue.toObject().value("text").toString();
    }
    if (out.isEmpty())
    {
        out = holder.value("simpleText").toString();
    }
    return out;
}

YouTubeAuthor parseAuthor(const QJsonObject &r)
{
    YouTubeAuthor author;
    author.channelId = r.value("authorExternalChannelId").toString();
    author.name = runsToText(r.value("authorName").toObject());
    author.avatarUrl = largestThumbnail(r.value("authorPhoto").toObject());

    for (const auto &badgeValue : r.value("authorBadges").toArray())
    {
        auto badge = badgeValue.toObject()
                         .value("liveChatAuthorBadgeRenderer")
                         .toObject();
        auto iconType =
            badge.value("icon").toObject().value("iconType").toString();

        if (iconType == QStringLiteral("OWNER"))
        {
            author.isOwner = true;
        }
        else if (iconType == QStringLiteral("MODERATOR"))
        {
            author.isModerator = true;
        }
        else if (iconType == QStringLiteral("VERIFIED"))
        {
            author.isVerified = true;
        }
        else if (badge.contains("customThumbnail"))
        {
            // Channel members get a tier-specific image rather than an icon.
            author.isMember = true;
            author.memberBadgeUrl =
                largestThumbnail(badge.value("customThumbnail").toObject());
            author.memberBadgeTooltip = badge.value("tooltip").toString();
        }
    }

    return author;
}

void parseCommonFields(const QJsonObject &r, YouTubeMessage &msg)
{
    msg.id = r.value("id").toString();
    msg.author = parseAuthor(r);

    bool ok = false;
    auto usec = r.value("timestampUsec").toString().toLongLong(&ok);
    msg.timestamp = ok ? QDateTime::fromMSecsSinceEpoch(usec / 1000)
                       : QDateTime::currentDateTime();
}

/// Turns one item of an addChatItemAction into a message, or returns nullopt
/// for renderers we deliberately ignore (engagement nags, placeholders).
std::optional<YouTubeMessage> parseChatItem(const QJsonObject &item)
{
    if (auto r = item.value("liveChatTextMessageRenderer").toObject();
        !r.isEmpty())
    {
        YouTubeMessage msg;
        parseCommonFields(r, msg);
        msg.kind = YouTubeItemKind::Text;
        msg.runs = parseRuns(r.value("message").toObject());
        return msg;
    }

    if (auto r = item.value("liveChatPaidMessageRenderer").toObject();
        !r.isEmpty())
    {
        YouTubeMessage msg;
        parseCommonFields(r, msg);
        msg.kind = YouTubeItemKind::SuperChat;
        msg.runs = parseRuns(r.value("message").toObject());
        msg.purchaseAmount = runsToText(r.value("purchaseAmountText").toObject());
        msg.bodyColor = colorFromArgb(r.value("bodyBackgroundColor"));
        return msg;
    }

    if (auto r = item.value("liveChatPaidStickerRenderer").toObject();
        !r.isEmpty())
    {
        YouTubeMessage msg;
        parseCommonFields(r, msg);
        msg.kind = YouTubeItemKind::SuperSticker;
        msg.purchaseAmount = runsToText(r.value("purchaseAmountText").toObject());
        msg.bodyColor = colorFromArgb(r.value("backgroundColor"));
        msg.eventHeadline = QStringLiteral("sent a Super Sticker");
        return msg;
    }

    if (auto r = item.value("liveChatMembershipItemRenderer").toObject();
        !r.isEmpty())
    {
        YouTubeMessage msg;
        parseCommonFields(r, msg);
        // A milestone chat carries a message body, a brand new membership
        // only carries the header.
        auto primary = runsToText(r.value("headerPrimaryText").toObject());
        auto subtext = runsToText(r.value("headerSubtext").toObject());
        msg.kind = primary.isEmpty() ? YouTubeItemKind::NewMember
                                     : YouTubeItemKind::MemberMilestone;
        msg.eventHeadline = primary.isEmpty() ? subtext : primary;
        msg.runs = parseRuns(r.value("message").toObject());
        return msg;
    }

    if (auto r =
            item.value("liveChatSponsorshipsGiftPurchaseAnnouncementRenderer")
                .toObject();
        !r.isEmpty())
    {
        auto header = r.value("header")
                          .toObject()
                          .value("liveChatSponsorshipsHeaderRenderer")
                          .toObject();
        YouTubeMessage msg;
        parseCommonFields(r, msg);
        // The author lives on the header for gift announcements.
        if (msg.author.name.isEmpty())
        {
            msg.author = parseAuthor(header);
        }
        msg.kind = YouTubeItemKind::GiftPurchase;
        msg.eventHeadline = runsToText(header.value("primaryText").toObject());
        return msg;
    }

    if (auto r =
            item.value("liveChatSponsorshipsGiftRedemptionAnnouncementRenderer")
                .toObject();
        !r.isEmpty())
    {
        YouTubeMessage msg;
        parseCommonFields(r, msg);
        msg.kind = YouTubeItemKind::GiftRedemption;
        msg.eventHeadline = runsToText(r.value("message").toObject());
        return msg;
    }

    return std::nullopt;
}

}  // namespace

namespace chatterino {

YouTubeInnerTube::YouTubeInnerTube(QObject *parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
}

YouTubeInnerTube::~YouTubeInnerTube() = default;

QString YouTubeInnerTube::userAgent()
{
    return BROWSER_UA;
}

void YouTubeInnerTube::resolveVideoId(
    const QString &input, std::function<void(QString, QString)> cb)
{
    auto trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
        cb({}, QStringLiteral("No channel given"));
        return;
    }

    static const QRegularExpression bareId{
        QStringLiteral("^[A-Za-z0-9_-]{11}$")};
    if (bareId.match(trimmed).hasMatch())
    {
        cb(trimmed, {});
        return;
    }

    // Strip a leading scheme so the rest can be treated uniformly.
    auto url = trimmed;
    url.remove(QRegularExpression{QStringLiteral("^https?://")});
    url.remove(QRegularExpression{QStringLiteral("^(www|m)\\.")});

    if (url.startsWith(QStringLiteral("youtu.be/")))
    {
        auto id = url.mid(9).section('?', 0, 0).section('/', 0, 0);
        cb(id, id.isEmpty() ? QStringLiteral("Could not read a video id") : QString{});
        return;
    }

    if (url.startsWith(QStringLiteral("youtube.com/")))
    {
        auto path = url.mid(12);

        if (path.startsWith(QStringLiteral("watch")))
        {
            QUrl parsed{QStringLiteral("https://www.youtube.com/") + path};
            auto id = QUrlQuery{parsed}.queryItemValue(QStringLiteral("v"));
            cb(id, id.isEmpty() ? QStringLiteral("Could not read a video id")
                                : QString{});
            return;
        }

        if (path.startsWith(QStringLiteral("live/")))
        {
            auto id = path.mid(5).section('?', 0, 0);
            cb(id, id.isEmpty() ? QStringLiteral("Could not read a video id")
                                : QString{});
            return;
        }

        // /@handle, /channel/UC..., /c/name, /user/name: all resolvable
        // through their /live page.
        auto base = path.section('?', 0, 0);
        if (base.endsWith('/'))
        {
            base.chop(1);
        }
        if (base.endsWith(QStringLiteral("/live")))
        {
            base.chop(5);
        }
        this->fetchLiveVideoIdForChannel(base, std::move(cb));
        return;
    }

    // A bare handle or channel name.
    auto handle = trimmed.startsWith('@') ? trimmed : ('@' + trimmed);
    this->fetchLiveVideoIdForChannel(handle, std::move(cb));
}

void YouTubeInnerTube::fetchLiveVideoIdForChannel(
    const QString &path, std::function<void(QString, QString)> cb)
{
    auto url = QStringLiteral("https://www.youtube.com/%1/live").arg(path);
    auto *reply = this->network_->get(pageRequest(url));

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb, path] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            cb({}, QStringLiteral("Could not reach YouTube: %1")
                       .arg(reply->errorString()));
            return;
        }

        auto html = QString::fromUtf8(reply->readAll());

        // The canonical link is the reliable one: it points at the stream that
        // is live right now, where the first "videoId" in the page can be a
        // sidebar recommendation.
        auto id = matchFirst(
            html, QStringLiteral(
                      R"RX(<link rel="canonical" href="[^"]*watch\?v=([A-Za-z0-9_-]{11}))RX"));
        if (id.isEmpty())
        {
            id = matchFirst(html,
                            QStringLiteral(R"RX("videoId":"([A-Za-z0-9_-]{11})")RX"));
        }

        if (id.isEmpty())
        {
            cb({}, QStringLiteral("%1 does not look like it is live").arg(path));
            return;
        }

        cb(id, {});
    });
}

void YouTubeInnerTube::openSession(
    const QString &videoId,
    std::function<void(YouTubeChatSession, QString)> cb)
{
    auto url =
        QStringLiteral("https://www.youtube.com/live_chat?v=%1").arg(videoId);
    auto *reply = this->network_->get(pageRequest(url));

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb,
                                                             videoId] {
        reply->deleteLater();

        YouTubeChatSession session;
        session.videoId = videoId;

        if (reply->error() != QNetworkReply::NoError)
        {
            cb(session, QStringLiteral("Could not reach YouTube: %1")
                            .arg(reply->errorString()));
            return;
        }

        auto html = QString::fromUtf8(reply->readAll());

        session.apiKey =
            matchFirst(html, QStringLiteral(R"RX(INNERTUBE_API_KEY":"([^"]+))RX"));
        session.clientVersion = matchFirst(
            html, QStringLiteral(R"RX(INNERTUBE_CLIENT_VERSION":"([^"]+))RX"));

        auto data = extractJsonBlob(html, QStringLiteral("ytInitialData"));
        if (data.isEmpty() || session.apiKey.isEmpty())
        {
            cb(session,
               QStringLiteral("YouTube did not return a chat page for this "
                              "stream"));
            return;
        }

        QMap<QString, QList<QJsonObject>> tokens;
        collectContinuations(data, tokens);

        // Prefer a reload token: it replays the recent backlog so the split is
        // not empty on join. The live tail only yields messages from now on.
        // The view selector lists "Top chat" first and unfiltered "Live chat"
        // last, and we want the unfiltered one.
        const auto reload = tokens.value(QStringLiteral("reloadContinuationData"));
        if (!reload.isEmpty())
        {
            session.continuation =
                reload.last().value("continuation").toString();
        }
        else
        {
            const auto tail =
                tokens.value(QStringLiteral("invalidationContinuationData"));
            if (!tail.isEmpty())
            {
                session.continuation =
                    tail.first().value("continuation").toString();
            }
        }

        if (session.continuation.isEmpty())
        {
            // The page renders a plain message when chat is off or the stream
            // is over; surfacing it verbatim beats a generic failure.
            auto reason = runsToText(
                data.value("contents").toObject().value("messageRenderer").toObject().value("text").toObject());
            cb(session, reason.isEmpty()
                            ? QStringLiteral("This stream has no live chat")
                            : reason);
            return;
        }

        cb(session, {});
    });
}

void YouTubeInnerTube::poll(const YouTubeChatSession &session,
                            std::function<void(YouTubeChatPoll)> cb)
{
    QJsonObject client{
        {"clientName", "WEB"},
        {"clientVersion", session.clientVersion.isEmpty()
                              ? QStringLiteral("2.20260904.01.00")
                              : session.clientVersion},
    };
    QJsonObject body{
        {"context", QJsonObject{{"client", client}}},
        {"continuation", session.continuation},
    };

    QNetworkRequest req{QUrl{INNERTUBE_CHAT_URL.arg(session.apiKey)}};
    req.setHeader(QNetworkRequest::UserAgentHeader, BROWSER_UA);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));

    auto *reply =
        this->network_->post(req, QJsonDocument{body}.toJson(QJsonDocument::Compact));

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb] {
        reply->deleteLater();

        YouTubeChatPoll result;

        if (reply->error() != QNetworkReply::NoError)
        {
            auto status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            result.error = reply->errorString();
            // A 404 or 400 here means the continuation is dead: the stream
            // ended or chat was turned off. Retrying will never recover.
            result.fatal = (status == 404 || status == 400);
            cb(result);
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto lcc = doc.object()
                       .value("continuationContents")
                       .toObject()
                       .value("liveChatContinuation")
                       .toObject();

        if (lcc.isEmpty())
        {
            result.error = QStringLiteral("Live chat has ended");
            result.fatal = true;
            cb(result);
            return;
        }

        for (const auto &actionValue : lcc.value("actions").toArray())
        {
            auto action = actionValue.toObject();

            // Replays wrap the real action one level deeper.
            if (action.contains("replayChatItemAction"))
            {
                auto inner = action.value("replayChatItemAction")
                                 .toObject()
                                 .value("actions")
                                 .toArray();
                if (!inner.isEmpty())
                {
                    action = inner.first().toObject();
                }
            }

            if (action.contains("addChatItemAction"))
            {
                auto item = action.value("addChatItemAction")
                                .toObject()
                                .value("item")
                                .toObject();
                if (auto msg = parseChatItem(item))
                {
                    result.messages.push_back(std::move(*msg));
                }
            }
            else if (action.contains("markChatItemAsDeletedAction"))
            {
                result.deletedMessageIds.push_back(
                    action.value("markChatItemAsDeletedAction")
                        .toObject()
                        .value("targetItemId")
                        .toString());
            }
            else if (action.contains("removeChatItemAction"))
            {
                result.deletedMessageIds.push_back(
                    action.value("removeChatItemAction")
                        .toObject()
                        .value("targetItemId")
                        .toString());
            }
            else if (action.contains(
                         "markChatItemsByAuthorAsDeletedAction"))
            {
                result.bannedAuthorIds.push_back(
                    action.value("markChatItemsByAuthorAsDeletedAction")
                        .toObject()
                        .value("externalChannelId")
                        .toString());
            }
        }

        auto continuations = lcc.value("continuations").toArray();
        if (!continuations.isEmpty())
        {
            auto next = continuations.first().toObject();
            for (auto it = next.begin(); it != next.end(); ++it)
            {
                auto data = it.value().toObject();
                if (!data.contains("continuation"))
                {
                    continue;
                }
                result.nextContinuation =
                    data.value("continuation").toString();
                auto timeout = data.value("timeoutMs").toInt();
                if (timeout > 0)
                {
                    result.timeoutMs = timeout;
                }
                break;
            }
        }

        if (result.nextContinuation.isEmpty())
        {
            result.error = QStringLiteral("Live chat has ended");
            result.fatal = true;
            cb(result);
            return;
        }

        result.ok = true;
        cb(result);
    });
}

}  // namespace chatterino
