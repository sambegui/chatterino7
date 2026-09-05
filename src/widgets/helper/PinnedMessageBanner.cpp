#include "widgets/helper/PinnedMessageBanner.hpp"

#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Theme.hpp"
#include "util/FormatTime.hpp"
#include "widgets/buttons/LabelButton.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

namespace chatterino {

PinnedMessageBanner::PinnedMessageBanner(QWidget *parent)
    : BaseWidget(parent)
{
    // without this the stylesheet background is not painted on a plain QWidget
    this->setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 4, 4);
    layout->setSpacing(6);

    this->author_ = new QLabel(this);
    this->text_ = new QLabel(this);
    this->remaining_ = new QLabel(this);
    this->dismiss_ = new LabelButton("x", this);

    // a long pin must not push the split wider
    this->text_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    this->text_->setTextFormat(Qt::PlainText);
    this->author_->setTextFormat(Qt::PlainText);

    layout->addWidget(this->author_);
    layout->addWidget(this->text_, 1);
    layout->addWidget(this->remaining_);
    layout->addWidget(this->dismiss_);

    QObject::connect(this->dismiss_, &Button::leftClicked, [this] {
        if (auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get()))
        {
            if (auto pin = twitch->pinnedMessage())
            {
                this->dismissedPinId_ = pin->pinId;
            }
        }
        this->hide();
    });

    this->countdown_ = new QTimer(this);
    this->countdown_->setInterval(1000);
    QObject::connect(this->countdown_, &QTimer::timeout, [this] {
        this->updateCountdown();
    });

    this->applyStyle();
    this->hide();
}

void PinnedMessageBanner::setChannel(ChannelPtr channel)
{
    this->channelConnections_.clear();
    this->channel_ = std::move(channel);
    this->dismissedPinId_.clear();

    if (auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get()))
    {
        this->channelConnections_.managedConnect(twitch->pinnedMessageChanged,
                                                 [this] {
                                                     this->refresh();
                                                 });
    }

    this->refresh();
}

void PinnedMessageBanner::refresh()
{
    auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get());
    if (twitch == nullptr)
    {
        this->countdown_->stop();
        this->hide();
        return;
    }

    auto pin = twitch->pinnedMessage();
    if (!pin || pin->pinId == this->dismissedPinId_)
    {
        this->countdown_->stop();
        this->hide();
        return;
    }

    this->author_->setText(pin->authorName.isEmpty() ? pin->authorLogin
                                                     : pin->authorName);

    QColor authorColor(pin->authorColor);
    this->author_->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; color: %1 }")
            .arg(authorColor.isValid()
                     ? authorColor.name()
                     : this->theme->messages.textColors.regular.name()));

    this->text_->setText(pin->text);
    this->text_->setToolTip(pin->text);

    this->endsAt_ = pin->endsAt;
    this->updateCountdown();
    if (this->endsAt_.isValid())
    {
        this->countdown_->start();
    }
    else
    {
        this->countdown_->stop();
    }

    this->show();
}

void PinnedMessageBanner::updateCountdown()
{
    if (!this->endsAt_.isValid())
    {
        this->remaining_->hide();
        return;
    }

    auto secondsLeft =
        QDateTime::currentDateTimeUtc().secsTo(this->endsAt_.toUTC());
    if (secondsLeft <= 0)
    {
        // Twitch does not always send an unpin event when a pin simply expires
        this->countdown_->stop();
        this->hide();
        return;
    }

    this->remaining_->setText(formatTime(static_cast<int>(secondsLeft)));
    this->remaining_->show();
}

void PinnedMessageBanner::applyStyle()
{
    this->setStyleSheet(
        QStringLiteral("chatterino--PinnedMessageBanner { background: %1 }")
            .arg(this->theme->messages.backgrounds.alternate.name()));
    this->text_->setStyleSheet(
        QStringLiteral("QLabel { color: %1 }")
            .arg(this->theme->messages.textColors.regular.name()));
    this->remaining_->setStyleSheet(
        QStringLiteral("QLabel { color: %1 }")
            .arg(this->theme->messages.textColors.system.name()));
}

void PinnedMessageBanner::themeChangedEvent()
{
    BaseWidget::themeChangedEvent();
    this->applyStyle();
    this->refresh();
}

void PinnedMessageBanner::scaleChangedEvent(float scale)
{
    BaseWidget::scaleChangedEvent(scale);
    this->refresh();
}

}  // namespace chatterino
