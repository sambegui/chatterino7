#pragma once

#include "widgets/BaseWidget.hpp"

#include <memory>

namespace chatterino {

class Channel;
class PinnedMessageBanner;
class PollBanner;
class PredictionBanner;
using ChannelPtr = std::shared_ptr<Channel>;

/// Stacks the channel event banners above the chat.
///
/// Each banner decides on its own whether it has anything to show. This only
/// fixes their order and keeps the whole strip out of the layout while they are
/// all empty.
class SplitBanners : public BaseWidget
{
public:
    explicit SplitBanners(QWidget *parent = nullptr);

    void setChannel(const ChannelPtr &channel);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateVisibility();

    PinnedMessageBanner *pinnedMessage_ = nullptr;
    PredictionBanner *prediction_ = nullptr;
    PollBanner *poll_ = nullptr;
};

}  // namespace chatterino
