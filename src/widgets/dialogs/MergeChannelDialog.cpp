#include "widgets/dialogs/MergeChannelDialog.hpp"

#include "channels/MergedChannel.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/youtube/YouTubeChannel.hpp"
#include "providers/youtube/YouTubeChatServer.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"

#include <QHBoxLayout>
#include <QMessageBox>

namespace chatterino {

MergeChannelDialog::MergeChannelDialog(ChannelPtr sourceChannel,
                                       QWidget *parent)
    : QDialog(parent)
    , sourceChannel_(std::move(sourceChannel))
{
    this->setWindowTitle("Merge Channels");
    this->setMinimumWidth(350);
    this->setupUI();
    this->updateSuggestion();
}

ChannelPtr MergeChannelDialog::getSelectedChannel() const
{
    return this->selectedChannel_;
}

MergeViewType MergeChannelDialog::getMergeViewType() const
{
    return this->mergeViewType_;
}

void MergeChannelDialog::setOnMerge(
    std::function<void(ChannelPtr, ChannelPtr)> callback)
{
    this->onMergeCallback_ = std::move(callback);
}

void MergeChannelDialog::setOnSplitView(
    std::function<void(ChannelPtr, ChannelPtr)> callback)
{
    this->onSplitViewCallback_ = std::move(callback);
}

void MergeChannelDialog::setupUI()
{
    this->mainLayout_ = new QVBoxLayout(this);

    // Source channel info
    QString sourcePlatform;
    QString sourceChannelName = this->sourceChannel_->getName();

    auto *mergedSource =
        dynamic_cast<MergedChannel *>(this->sourceChannel_.get());

    if (mergedSource != nullptr)
    {
        sourcePlatform = "Merged";
        // A merge shows its own combined name; suggesting a channel to add is
        // more useful when based on one of its existing sources.
        const auto &sources = mergedSource->getSourceChannels();
        if (!sources.empty())
        {
            sourceChannelName = sources.front()->getName();
        }
    }
    else if (dynamic_cast<TwitchChannel *>(this->sourceChannel_.get()))
    {
        sourcePlatform = "Twitch";
    }
    else if (dynamic_cast<KickChannel *>(this->sourceChannel_.get()))
    {
        sourcePlatform = "Kick";
    }
    else if (dynamic_cast<YouTubeChannel *>(this->sourceChannel_.get()))
    {
        sourcePlatform = "YouTube";
    }
    else
    {
        sourcePlatform = "Unknown";
    }

    this->suggestedName_ = sourceChannelName;

    this->sourceLabel_ = new QLabel(
        mergedSource != nullptr
            ? QString("Source: %1").arg(this->sourceChannel_->getDisplayName())
            : QString("Source: %1 - %2").arg(sourcePlatform,
                                             sourceChannelName));
    this->sourceLabel_->setStyleSheet("font-weight: bold;");
    this->mainLayout_->addWidget(this->sourceLabel_);

    // Target platform selection
    this->targetLabel_ = new QLabel("Merge with:");
    this->mainLayout_->addWidget(this->targetLabel_);

    auto *platformLayout = new QHBoxLayout();

    this->platformCombo_ = new QComboBox();

    // Offer every platform the source does not already carry. For a merge that
    // means every type not already in it, which is what makes adding YouTube to
    // a Twitch+Kick split possible.
    auto alreadyHave = [mergedSource, &sourcePlatform](Channel::Type type,
                                                       const QString &name) {
        if (mergedSource != nullptr)
        {
            return mergedSource->hasPlatform(type);
        }
        return sourcePlatform == name;
    };

    // A platform that is turned off is still listed, marked so, and offers to
    // enable itself on merge. Hiding it left the combo empty on a Twitch+Kick
    // merge with no hint that a third platform was available at all.
    auto addPlatform = [this](Channel::Type type, const QString &label,
                              const QString &data, bool present,
                              bool enabled) {
        Q_UNUSED(type);
        if (present)
        {
            return;
        }
        this->platformCombo_->addItem(
            enabled ? label : label + "  (enable in settings)", data);
    };

    addPlatform(Channel::Type::Twitch, "Twitch", "twitch",
                alreadyHave(Channel::Type::Twitch, "Twitch"), true);
    addPlatform(Channel::Type::Kick, "Kick", "kick",
                alreadyHave(Channel::Type::Kick, "Kick"),
                getSettings()->enableKickIntegration);
    addPlatform(Channel::Type::YouTube, "YouTube", "youtube",
                alreadyHave(Channel::Type::YouTube, "YouTube"),
                getSettings()->enableYouTubeIntegration);
    platformLayout->addWidget(this->platformCombo_);

    this->channelInput_ = new QLineEdit();
    this->channelInput_->setPlaceholderText("Channel name...");
    platformLayout->addWidget(this->channelInput_);

    this->mainLayout_->addLayout(platformLayout);

    // Auto-suggestion label
    this->suggestionLabel_ = new QLabel();
    this->suggestionLabel_->setStyleSheet("color: #888; font-style: italic;");
    this->mainLayout_->addWidget(this->suggestionLabel_);

    // Add spacing
    this->mainLayout_->addSpacing(10);

    // Merge type selection
    this->mergeTypeLabel_ = new QLabel("View type:");
    this->mergeTypeLabel_->setStyleSheet("font-weight: bold;");
    this->mainLayout_->addWidget(this->mergeTypeLabel_);

    this->mergeTypeGroup_ = new QButtonGroup(this);

    // Single view option (combined chat)
    this->singleViewRadio_ = new QRadioButton("Combined view (single panel)");
    this->singleViewRadio_->setChecked(true);
    this->mergeTypeGroup_->addButton(
        this->singleViewRadio_, static_cast<int>(MergeViewType::SingleView));
    this->mainLayout_->addWidget(this->singleViewRadio_);

    this->singleViewDescription_ = new QLabel(
        "   Messages interleaved in one panel, each tagged with its platform");
    this->singleViewDescription_->setStyleSheet(
        "color: #888; font-size: 11px; margin-left: 20px;");
    this->mainLayout_->addWidget(this->singleViewDescription_);

    // Split view option (side-by-side)
    this->splitViewRadio_ = new QRadioButton("Side-by-side view (two panels)");
    this->mergeTypeGroup_->addButton(
        this->splitViewRadio_, static_cast<int>(MergeViewType::SplitView));
    this->mainLayout_->addWidget(this->splitViewRadio_);

    this->splitViewDescription_ = new QLabel(
        "   Each channel in its own panel, displayed next to each other");
    this->splitViewDescription_->setStyleSheet(
        "color: #888; font-size: 11px; margin-left: 20px;");
    this->mainLayout_->addWidget(this->splitViewDescription_);

    // Add spacing before buttons
    this->mainLayout_->addSpacing(10);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();

    this->mergeButton_ = new QPushButton("Merge");
    this->mergeButton_->setDefault(true);
    buttonLayout->addWidget(this->mergeButton_);

    this->cancelButton_ = new QPushButton("Cancel");
    buttonLayout->addWidget(this->cancelButton_);

    this->mainLayout_->addLayout(buttonLayout);

    // Connections
    QObject::connect(this->channelInput_, &QLineEdit::textChanged, this,
                     &MergeChannelDialog::updateSuggestion);

    QObject::connect(this->mergeButton_, &QPushButton::clicked, this,
                     &MergeChannelDialog::onMergeClicked);

    QObject::connect(this->cancelButton_, &QPushButton::clicked, this,
                     &QDialog::reject);
}

void MergeChannelDialog::updateSuggestion()
{
    QString sourceChannelName = this->suggestedName_.isEmpty()
                                    ? this->sourceChannel_->getName()
                                    : this->suggestedName_;
    QString suggestedDisplay = sourceChannelName;
    sourceChannelName = sourceChannelName.toLower();
    QString inputText = this->channelInput_->text().trimmed().toLower();

    if (inputText.isEmpty())
    {
        // Auto-suggest matching channel name
        this->suggestionLabel_->setText(
            QString("Suggestion: Try '%1' (same username)")
                .arg(sourceChannelName));
        this->channelInput_->setText(suggestedDisplay);
    }
    else if (inputText == sourceChannelName)
    {
        this->suggestionLabel_->setText("✓ Matching username detected");
        this->suggestionLabel_->setStyleSheet(
            "color: #53fc18; font-style: italic;");
    }
    else
    {
        this->suggestionLabel_->setText(
            QString("Note: Merging '%1' with '%2'")
                .arg(suggestedDisplay, inputText));
        this->suggestionLabel_->setStyleSheet(
            "color: #888; font-style: italic;");
    }
}

void MergeChannelDialog::onMergeClicked()
{
    QString targetChannelName = this->channelInput_->text().trimmed();

    if (targetChannelName.isEmpty())
    {
        QMessageBox::warning(this, "Invalid Input",
                             "Please enter a channel name to merge with.");
        return;
    }

    QString platform = this->platformCombo_->currentData().toString();

    if (platform.isEmpty())
    {
        QMessageBox::warning(
            this, "Nothing left to merge",
            "This split already carries every platform you have enabled.\n\n"
            "Enable another platform in Settings > Platforms to add one.");
        return;
    }

    // Turning the platform on here keeps the merge flow going instead of
    // sending the user to the settings page and back.
    auto ensureEnabled = [this](BoolSetting &setting, const QString &name) {
        if (setting)
        {
            return true;
        }

        QMessageBox box;
        box.setWindowTitle(QString("%1 Integration Disabled").arg(name));
        box.setText(QString("%1 integration is currently disabled. Would you "
                            "like to enable it?")
                        .arg(name));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::Yes);
        if (box.exec() != QMessageBox::Yes)
        {
            return false;
        }
        setting.setValue(true);
        return true;
    };

    if (platform == "kick" &&
        !ensureEnabled(getSettings()->enableKickIntegration, "Kick"))
    {
        return;
    }
    if (platform == "youtube" &&
        !ensureEnabled(getSettings()->enableYouTubeIntegration, "YouTube"))
    {
        return;
    }

    // Get or create the target channel
    if (platform == "twitch")
    {
        this->selectedChannel_ =
            getApp()->getTwitch()->getOrAddChannel(targetChannelName);
    }
    else if (platform == "youtube")
    {
        auto youtubeChannel =
            getApp()->getYouTubeChatServer()->getOrCreate(targetChannelName);
        youtubeChannel->connect();
        this->selectedChannel_ = ChannelPtr(youtubeChannel);
    }
    else if (platform == "kick")
    {
        // Create KickChannel with account and API for sending messages
        auto kickChannel =
            getApp()->getKickChatServer()->getOrCreate(targetChannelName);

        // Set up authentication from current Kick account
        auto kickAccount = getApp()->getAccounts()->kick.getCurrent();
        if (kickAccount && kickAccount->isAuthenticated())
        {
            kickChannel->setAccount(kickAccount);
            kickChannel->setAuthenticated(true);

            // Create API instance with account
            auto kickApi = std::make_shared<KickApi>();
            kickApi->setAccount(kickAccount);
            kickChannel->setApi(kickApi);
        }

        // Connect the kick channel to start receiving messages
        kickChannel->connect();

        this->selectedChannel_ = kickChannel;
    }

    // Determine which merge type was selected
    int selectedId = this->mergeTypeGroup_->checkedId();
    this->mergeViewType_ = static_cast<MergeViewType>(selectedId);

    if (this->selectedChannel_)
    {
        if (this->mergeViewType_ == MergeViewType::SingleView &&
            this->onMergeCallback_)
        {
            // Combined view - create a MergedChannel
            this->onMergeCallback_(this->sourceChannel_,
                                   this->selectedChannel_);
        }
        else if (this->mergeViewType_ == MergeViewType::SplitView &&
                 this->onSplitViewCallback_)
        {
            // Side-by-side view - open target channel in new split
            this->onSplitViewCallback_(this->sourceChannel_,
                                       this->selectedChannel_);
        }
    }

    this->accept();
}

}  // namespace chatterino
