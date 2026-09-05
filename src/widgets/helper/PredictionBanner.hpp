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

/// Strip showing the prediction running in a channel, with a row per outcome.
///
/// Stays visible while the prediction is locked so viewers can see it resolve,
/// and hides once it is resolved or canceled.
class PredictionBanner : public BaseWidget
{
public:
    explicit PredictionBanner(QWidget *parent = nullptr);

    void setChannel(ChannelPtr channel);

protected:
    void themeChangedEvent() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void updateCountdown();
    void predictOn(size_t outcomeIndex);

    ChannelPtr channel_;
    pajlada::Signals::SignalHolder channelConnections_;

    QLabel *title_ = nullptr;
    QLabel *remaining_ = nullptr;
    QVBoxLayout *outcomes_ = nullptr;
    QTimer *countdown_ = nullptr;

    std::vector<QLabel *> outcomeLabels_;
    QString eventId_;
    std::vector<QString> outcomeIds_;
    bool acceptsEntries_ = false;
    QDateTime locksAt_;
};

}  // namespace chatterino
