#pragma once

#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>

#include <memory>
#include <vector>

class QLabel;
class QTimer;
class QEvent;
class QVBoxLayout;

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

/// Strip showing the poll running in a channel, with a bar per choice.
///
/// Hides itself when no poll is running, so it can stay in the layout.
class PollBanner : public BaseWidget
{
public:
    explicit PollBanner(QWidget *parent = nullptr);

    void setChannel(ChannelPtr channel);

protected:
    void themeChangedEvent() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void updateCountdown();
    void voteFor(size_t choiceIndex);

    ChannelPtr channel_;
    pajlada::Signals::SignalHolder channelConnections_;

    QLabel *title_ = nullptr;
    QLabel *remaining_ = nullptr;
    QVBoxLayout *choices_ = nullptr;
    QTimer *countdown_ = nullptr;

    /// One row per choice, reused across updates so votes can tick up without
    /// rebuilding the layout.
    std::vector<QLabel *> choiceLabels_;
    /// The poll each row belongs to, so a stale click cannot vote in a new one.
    QString pollId_;
    std::vector<QString> choiceIds_;
    QDateTime endsAt_;
};

}  // namespace chatterino
