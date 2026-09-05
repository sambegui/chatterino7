#include "widgets/helper/PollBanner.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Theme.hpp"
#include "util/FormatTime.hpp"

#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/// A poll shows totals, not a chart, so a share is enough of a bar.
QString formatChoice(const chatterino::TwitchPollChoice &choice, int totalVotes)
{
    auto share = totalVotes > 0 ? (choice.votes * 100) / totalVotes : 0;
    return QStringLiteral("%1  %2%  (%3)")
        .arg(choice.title)
        .arg(share)
        .arg(choice.votes);
}

}  // namespace

namespace chatterino {

PollBanner::PollBanner(QWidget *parent)
    : BaseWidget(parent)
{
    this->setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    auto *header = new QHBoxLayout();
    this->title_ = new QLabel(this);
    this->remaining_ = new QLabel(this);
    this->title_->setTextFormat(Qt::PlainText);
    header->addWidget(this->title_, 1);
    header->addWidget(this->remaining_);
    layout->addLayout(header);

    this->choices_ = new QVBoxLayout();
    this->choices_->setSpacing(1);
    layout->addLayout(this->choices_);

    this->countdown_ = new QTimer(this);
    this->countdown_->setInterval(1000);
    QObject::connect(this->countdown_, &QTimer::timeout, [this] {
        this->updateCountdown();
    });

    this->themeChangedEvent();
    this->hide();
}

void PollBanner::setChannel(ChannelPtr channel)
{
    this->channelConnections_.clear();
    this->channel_ = std::move(channel);

    if (auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get()))
    {
        this->channelConnections_.managedConnect(twitch->pollChanged, [this] {
            this->refresh();
        });
    }

    this->refresh();
}

void PollBanner::refresh()
{
    auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get());
    if (twitch == nullptr)
    {
        this->countdown_->stop();
        this->hide();
        return;
    }

    auto poll = twitch->poll();
    if (!poll || !poll->isActive())
    {
        this->countdown_->stop();
        this->hide();
        return;
    }

    this->title_->setText(poll->title);

    // grow the row pool to fit, then hide whatever this poll does not use
    while (this->choiceLabels_.size() < poll->choices.size())
    {
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->installEventFilter(this);
        this->choices_->addWidget(label);
        this->choiceLabels_.push_back(label);
    }

    this->pollId_ = poll->id;
    this->choiceIds_.clear();
    for (const auto &choice : poll->choices)
    {
        this->choiceIds_.push_back(choice.id);
    }

    auto canVote = gql::isEnabled();

    for (size_t i = 0; i < this->choiceLabels_.size(); i++)
    {
        auto *label = this->choiceLabels_[i];
        if (i < poll->choices.size())
        {
            label->setText(formatChoice(poll->choices[i], poll->totalVotes));
            label->setCursor(canVote ? Qt::PointingHandCursor
                                     : Qt::ArrowCursor);
            label->setToolTip(canVote ? "Click to vote" : QString());
            label->show();
        }
        else
        {
            label->hide();
        }
    }

    this->endsAt_ = poll->endsAt;
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

void PollBanner::updateCountdown()
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
        // the poll is over, wait for the end event to say how it landed
        this->countdown_->stop();
        this->remaining_->hide();
        return;
    }

    this->remaining_->setText(formatTime(static_cast<int>(secondsLeft)));
    this->remaining_->show();
}

bool PollBanner::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
    {
        for (size_t i = 0; i < this->choiceLabels_.size(); i++)
        {
            if (this->choiceLabels_[i] == watched)
            {
                this->voteFor(i);
                return true;
            }
        }
    }

    return BaseWidget::eventFilter(watched, event);
}

void PollBanner::voteFor(size_t choiceIndex)
{
    if (choiceIndex >= this->choiceIds_.size() || this->pollId_.isEmpty())
    {
        return;
    }

    auto channel = this->channel_;
    auto unavailable = gql::unavailableReason();
    if (!unavailable.isEmpty())
    {
        channel->addSystemMessage(unavailable);
        return;
    }

    auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon())
    {
        channel->addSystemMessage("You must be logged in to vote in a poll.");
        return;
    }

    gql::voteInPoll(
        this->pollId_, this->choiceIds_[choiceIndex], account->getUserId(),
        [channel] {
            channel->addSystemMessage("Voted.");
        },
        [channel](const QString &error) {
            channel->addSystemMessage("Could not vote: " + error);
        });
}

void PollBanner::themeChangedEvent()
{
    BaseWidget::themeChangedEvent();

    this->setStyleSheet(
        QStringLiteral("chatterino--PollBanner { background: %1 }")
            .arg(this->theme->messages.backgrounds.alternate.name()));
    this->title_->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; color: %1 }")
            .arg(this->theme->messages.textColors.regular.name()));
    this->remaining_->setStyleSheet(
        QStringLiteral("QLabel { color: %1 }")
            .arg(this->theme->messages.textColors.system.name()));

    for (auto *label : this->choiceLabels_)
    {
        label->setStyleSheet(
            QStringLiteral("QLabel { color: %1 }")
                .arg(this->theme->messages.textColors.regular.name()));
    }
}

}  // namespace chatterino
