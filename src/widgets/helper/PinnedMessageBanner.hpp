#pragma once

#include "providers/twitch/TwitchPinnedChat.hpp"
#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>

#include <optional>

class QLabel;
class QTimer;

namespace chatterino {

class Channel;
class LabelButton;
using ChannelPtr = std::shared_ptr<Channel>;

/// Strip shown above the chat while a message is pinned in the channel.
///
/// Hides itself when nothing is pinned, so it can stay in the layout.
class PinnedMessageBanner : public BaseWidget
{
public:
    explicit PinnedMessageBanner(QWidget *parent = nullptr);

    /// Rebinds the banner to follow another channel. Pass an empty channel to
    /// detach.
    void setChannel(ChannelPtr channel);

protected:
    void themeChangedEvent() override;
    void scaleChangedEvent(float scale) override;

private:
    void refresh();
    void updateCountdown();
    void applyStyle();

    ChannelPtr channel_;
    pajlada::Signals::SignalHolder channelConnections_;

    QLabel *author_ = nullptr;
    QLabel *text_ = nullptr;
    QLabel *remaining_ = nullptr;
    LabelButton *dismiss_ = nullptr;
    QTimer *countdown_ = nullptr;

    /// Pin the user dismissed, so it stays hidden until a different one lands.
    QString dismissedPinId_;
    QDateTime endsAt_;
};

}  // namespace chatterino
