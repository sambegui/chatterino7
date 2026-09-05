#include "widgets/helper/SplitBanners.hpp"

#include "channels/MergedChannel.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "widgets/helper/PinnedMessageBanner.hpp"
#include "widgets/helper/PollBanner.hpp"
#include "widgets/helper/PredictionBanner.hpp"

#include <QEvent>
#include <QVBoxLayout>

namespace chatterino {

SplitBanners::SplitBanners(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    // most transient first, so a pin does not jump around as polls come and go
    this->pinnedMessage_ = new PinnedMessageBanner(this);
    this->prediction_ = new PredictionBanner(this);
    this->poll_ = new PollBanner(this);

    for (auto *banner : {static_cast<QWidget *>(this->pinnedMessage_),
                         static_cast<QWidget *>(this->prediction_),
                         static_cast<QWidget *>(this->poll_)})
    {
        layout->addWidget(banner);
        banner->installEventFilter(this);
    }

    this->hide();
}

void SplitBanners::setChannel(const ChannelPtr &channel)
{
    // all three of these describe a Twitch channel, and a merged split carries
    // one rather than being one
    auto target = channel;
    if (auto *merged = dynamic_cast<MergedChannel *>(channel.get()))
    {
        for (const auto &source : merged->getSourceChannels())
        {
            if (dynamic_cast<TwitchChannel *>(source.get()) != nullptr)
            {
                target = source;
                break;
            }
        }
    }

    this->pinnedMessage_->setChannel(target);
    this->prediction_->setChannel(target);
    this->poll_->setChannel(target);

    this->updateVisibility();
}

bool SplitBanners::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
    {
        this->updateVisibility();
    }

    return BaseWidget::eventFilter(watched, event);
}

void SplitBanners::updateVisibility()
{
    this->setVisible(!this->pinnedMessage_->isHidden() ||
                     !this->prediction_->isHidden() ||
                     !this->poll_->isHidden());
}

}  // namespace chatterino
