#include "widgets/settingspages/ChatPage.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"  // IWYU pragma: keep
#include "common/Version.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyCategory.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/CrashHandler.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/NativeMessaging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "util/FuzzyConvert.hpp"
#include "util/Helpers.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/helper/FontSettingWidget.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingsPageCommon.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>

namespace {

using namespace chatterino;
using namespace chatterino::literals;

}  // namespace


namespace chatterino {

ChatPage::ChatPage()
{
    this->buildLayout();
}

void ChatPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Chat");

    layout.addDropdown<float>(
        "Pause on mouse hover",
        {"Disabled", "0.5s", "1s", "2s", "5s", "Indefinite"},
        s.pauseOnHoverDuration,
        [](auto val) {
            if (val < -0.5f)
            {
                return QString("Indefinite");
            }
            else if (val < 0.001f)
            {
                return QString("Disabled");
            }
            else
            {
                return QString::number(val) + "s";
            }
        },
        [](auto args) {
            if (args.index == 0)
            {
                return 0.0f;
            }
            else if (args.value == "Indefinite")
            {
                return -1.0f;
            }
            else
            {
                return fuzzyToFloat(args.value,
                                    std::numeric_limits<float>::infinity());
            }
        });
    addKeyboardModifierSetting(layout, "Pause while holding a key",
                               s.pauseChatModifier);
    layout.addDropdown<float>(
        "Mousewheel scroll speed", {"0.5x", "0.75x", "Default", "1.5x", "2x"},
        s.mouseScrollMultiplier,
        [](auto val) {
            if (val == 1)
            {
                return QString("Default");
            }
            else
            {
                return QString::number(val) + "x";
            }
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 1.f);
        });

    SettingWidget::checkbox("Smooth scrolling", s.enableSmoothScrolling)
        ->addTo(layout);

    SettingWidget::checkbox("Smooth scrolling on new messages",
                            s.enableSmoothScrollingNewMessages)
        ->addTo(layout);

    SettingWidget::checkbox("Show input when it's empty", s.showEmptyInput)
        ->setTooltip("Show the chat box even when there is nothing typed.")
        ->addTo(layout);

    SettingWidget::checkbox("Show message length while typing",
                            s.showMessageLength)
        ->setTooltip(
            "Show how many characters are currently in your input box.\n"
            "Useful for making sure you don't go past the 500 character Twitch "
            "limit, or a lower limit enforced by a moderation bot")
        ->addTo(layout);

    SettingWidget::checkbox("Allow sending duplicate messages",
                            s.allowDuplicateMessages)
        ->setTooltip(
            "Allow a single message to be repeatedly sent without any changes.")
        ->addTo(layout);

    layout.addDropdown<std::underlying_type_t<MessageOverflow>>(
        "Message overflow", {"Highlight", "Prevent", "Allow"},
        s.messageOverflow,
        [](auto index) {
            return index;
        },
        [](auto args) {
            return static_cast<MessageOverflow>(args.index);
        },
        false,
        "Specify how Chatterino will handle messages that exceed Twitch "
        "message limits");
    layout.addDropdown<std::underlying_type_t<UsernameRightClickBehavior>>(
        "Username right-click behavior",
        {
            "Reply",
            "Mention",
            "Ignore",
        },
        s.usernameRightClickBehavior,
        [](auto index) {
            return index;
        },
        [](auto args) {
            return static_cast<UsernameRightClickBehavior>(args.index);
        },
        false,
        "Specify how Chatterino will handle right-clicking a username in "
        "chat when not holding the modifier.");
    layout.addDropdown<std::underlying_type_t<UsernameRightClickBehavior>>(
        "Username right-click with modifier behavior",
        {
            "Reply",
            "Mention",
            "Ignore",
        },
        s.usernameRightClickModifierBehavior,
        [](auto index) {
            return index;
        },
        [](auto args) {
            return static_cast<UsernameRightClickBehavior>(args.index);
        },
        false,
        "Specify how Chatterino will handle right-clicking a username in "
        "chat when holding down the modifier.");
    layout.addDropdown<std::underlying_type_t<Qt::KeyboardModifier>>(
        "Modifier for alternate right-click action",
        {"Shift", "Control", "Alt", META_KEY}, s.usernameRightClickModifier,
        [](int index) {
            switch (index)
            {
                case Qt::ShiftModifier:
                    return 0;
                case Qt::ControlModifier:
                    return 1;
                case Qt::AltModifier:
                    return 2;
                case Qt::MetaModifier:
                    return 3;
                default:
                    return 0;
            }
        },
        [](DropdownArgs args) {
            switch (args.index)
            {
                case 0:
                    return Qt::ShiftModifier;
                case 1:
                    return Qt::ControlModifier;
                case 2:
                    return Qt::AltModifier;
                case 3:
                    return Qt::MetaModifier;
                default:
                    return Qt::NoModifier;
            }
        },
        false);

    SettingWidget::checkbox("Hide scrollbar thumb", s.hideScrollbarThumb)
        ->setTooltip("Hiding the scrollbar thumb (the handle you can drag) "
                     "will disable all mouse interaction in the scrollbar.")
        ->addKeywords({"scroll bar"})
        ->addTo(layout);

    SettingWidget::checkbox("Hide scrollbar highlights",
                            s.hideScrollbarHighlights)
        ->addKeywords({"scroll bar"})
        ->addTo(layout);

    SettingWidget::checkbox(
        "Pulse text input when one of your messages is successfully sent",
        s.pulseTextInputOnSelfMessage)
        ->setTooltip(
            "Pulses the text input in a green color whenever a message of "
            "yours is successfully sent in the matching channel.")
        ->addTo(layout);

    layout.addTitle("Link Previews");
    layout.addDescription(
        "Extra information like \"youtube video stats\" or title of webpages "
        "can be loaded for all links if enabled. Optionally you can also show "
        "thumbnails for emotes, videos and more. The information is pulled "
        "from our servers. The Link Previews are loaded through <a "
        "href=\"https://github.com/Chatterino/api\">an API</a> hosted by the "
        "Chatterino developers. These are the API <a "
        "href=\"https://braize.pajlada.com/chatterino/legal/"
        "terms-of-service\">Terms of Services</a> and <a "
        "href=\"https://braize.pajlada.com/chatterino/legal/"
        "privacy-policy\">Privacy Policy</a>.");

    SettingWidget::checkbox("Enable", s.linkInfoTooltip)->addTo(layout);

    layout.addDropdown<int>(
        "Also show thumbnails if available",
        {"Off", "Small", "Medium", "Large"}, s.thumbnailSize,
        [](auto val) {
            if (val == 0)
            {
                return QString("Off");
            }
            else if (val == 100)
            {
                return QString("Small");
            }
            else if (val == 200)
            {
                return QString("Medium");
            }
            else if (val == 300)
            {
                return QString("Large");
            }
            else
            {
                return QString::number(val);
            }
        },
        [](auto args) {
            if (args.value == "Small")
            {
                return 100;
            }
            else if (args.value == "Medium")
            {
                return 200;
            }
            else if (args.value == "Large")
            {
                return 300;
            }

            return fuzzyToInt(args.value, 0);
        });
    layout.addDropdown<int>(
        "Show thumbnails of streams", {"Off", "Small", "Medium", "Large"},
        s.thumbnailSizeStream,
        [](auto val) {
            if (val == 0)
            {
                return QString("Off");
            }
            else if (val == 1)
            {
                return QString("Small");
            }
            else if (val == 2)
            {
                return QString("Medium");
            }
            else if (val == 3)
            {
                return QString("Large");
            }
            else
            {
                return QString::number(val);
            }
        },
        [](auto args) {
            if (args.value == "Small")
            {
                return 1;
            }
            else if (args.value == "Medium")
            {
                return 2;
            }
            else if (args.value == "Large")
            {
                return 3;
            }

            return fuzzyToInt(args.value, 0);
        });

    layout.addTitle("Channel events");
    layout.addDescription(
        "Show what a channel is running above the chat. These read Twitch's "
        "PubSub feed and need no login.");

    SettingWidget::checkbox("Show pinned messages", s.enablePinnedMessages)
        ->addTo(layout);
    SettingWidget::checkbox("Show polls", s.enablePolls)->addTo(layout);
    SettingWidget::checkbox("Show predictions", s.enablePredictions)
        ->addTo(layout);
    SettingWidget::checkbox("Show a name history button in usercards",
                            s.showUsercardNameHistoryButton)
        ->setTooltip(
            "Name history comes from a third-party logs service, not Twitch.")
        ->addTo(layout);

    layout.addTitle("Chat title");
    layout.addDescription("In live channels show:");
    SettingWidget::checkbox("Uptime", s.headerUptime)
        ->setTooltip("Show how long the channel has been live")
        ->addTo(layout);
    SettingWidget::checkbox("Viewer count", s.headerViewerCount)
        ->setTooltip("Show how many users are watching")
        ->addTo(layout);
    SettingWidget::checkbox("Category", s.headerGame)
        ->setTooltip("Show what Category the stream is listed under")
        ->addTo(layout);
    SettingWidget::checkbox("Title", s.headerStreamTitle)
        ->setTooltip("Show the stream title")
        ->addTo(layout);

    layout.addTitle("R9K");
    auto toggleLocalr9kSeq = getApp()->getHotkeys()->getDisplaySequence(
        HotkeyCategory::Window, "toggleLocalR9K");
    QString toggleLocalr9kShortcut =
        "an assigned hotkey (Window -> Toggle local R9K)";
    if (!toggleLocalr9kSeq.isEmpty())
    {
        toggleLocalr9kShortcut = toggleLocalr9kSeq.toString(
            QKeySequence::SequenceFormat::NativeText);
    }
    layout.addDescription("Hide similar messages to those previously seen. "
                          "Toggle hidden messages by pressing " +
                          toggleLocalr9kShortcut + ".");

    SettingWidget::checkbox("Enable similarity checks", s.similarityEnabled)
        ->addTo(layout);

    // SettingWidget::checkbox("Gray out matches", s.colorSimilarDisabled)->addTo(layout);

    SettingWidget::checkbox("Only if by the same user", s.hideSimilarBySameUser)
        ->setTooltip(
            "When checked, messages that are very similar to each other can "
            "still be shown as long as they're sent by different users.")
        ->addTo(layout);

    SettingWidget::checkbox("Hide my own messages", s.hideSimilarMyself)
        ->addTo(layout);

    SettingWidget::checkbox("Receive notification sounds from hidden messages",
                            s.shownSimilarTriggerHighlights)
        ->addTo(layout);

    s.hideSimilar.connect(
        []() {
            getApp()->getWindows()->forceLayoutChannelViews();
        },
        false);
    layout.addDropdown<float>(
        "Similarity threshold", {"0.5", "0.75", "0.9"}, s.similarityPercentage,
        [](auto val) {
            return QString::number(val);
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 0.9F);
        },
        true,
        "A value of 0.9 means the messages need to be 90% similar to be marked "
        "as similar.");
    layout.addDropdown<int>(
        "Maximum delay between messages",
        {"5s", "10s", "15s", "30s", "60s", "120s"}, s.hideSimilarMaxDelay,
        [](auto val) {
            return QString::number(val) + "s";
        },
        [](auto args) {
            return fuzzyToInt(args.value, 5);
        },
        true,
        "A value of 5s means if there's a 5s break between messages, we will "
        "stop looking further through the messages for similarities.");
    layout.addDropdown<int>(
        "Amount of previous messages to check", {"1", "2", "3", "4", "5"},
        s.hideSimilarMaxMessagesToCheck,
        [](auto val) {
            return QString::number(val);
        },
        [](auto args) {
            return fuzzyToInt(args.value, 3);
        },
        true,
        "How many messages in the history should be compared to a new one to "
        "establish its similarity rating. Messages in the history will be "
        "compared to only if they are new enough.");

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
