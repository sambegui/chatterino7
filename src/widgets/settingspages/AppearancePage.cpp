#include "widgets/settingspages/AppearancePage.hpp"

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

AppearancePage::AppearancePage()
{
    this->buildLayout();
}

void AppearancePage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Messages");

    SettingWidget::checkbox("Separate with lines", s.separateMessages)
        ->setTooltip(
            "Adds a line between each message to help better tell them apart.")
        ->addTo(layout);

    SettingWidget::checkbox("Alternate background color", s.alternateMessages)
        ->setTooltip("Slightly change the background behind every other "
                     "message to help better tell them apart.")
        ->addTo(layout);

    SettingWidget::checkbox("Reduce opacity of message history",
                            s.fadeMessageHistory)
        ->setTooltip(
            "Reduce opacity of messages that were posted before Chatterino "
            "was started or while re-connection.")
        ->addTo(layout);

    SettingWidget::checkbox("Hide deleted messages", s.hideModerated)
        ->setTooltip(
            "When enabled, messages deleted by moderators will be hidden.")
        ->addTo(layout);

    layout.addDropdown<QString>(
        "Message timestamp format",
        {"Disable", "h:mm", "hh:mm", "h:mm a", "hh:mm a", "h:mm:ss", "hh:mm:ss",
         "h:mm:ss a", "hh:mm:ss a", "h:mm:ss.zzz", "h:mm:ss.zzz a",
         "hh:mm:ss.zzz", "hh:mm:ss.zzz a"},
        s.timestampFormat,
        [](auto val) {
            return getSettings()->showTimestamps.getValue()
                       ? val
                       : QString("Disable");
        },
        [](auto args) {
            getSettings()->showTimestamps.setValue(args.index != 0);

            return args.index == 0 ? getSettings()->timestampFormat.getValue()
                                   : args.value;
        },
        true, "a = am/pm, zzz = milliseconds");
    layout.addDropdown<int>(
        "Limit message height",
        {"Never", "2 lines", "3 lines", "4 lines", "5 lines"},
        s.collpseMessagesMinLines,
        [](auto val) {
            return val ? QString::number(val) + " lines" : QString("Never");
        },
        [](auto args) {
            return fuzzyToInt(args.value, 0);
        });
    layout.addDropdown<int>(
        "Limit length of deleted messages",
        {"No limit", "50 characters", "100 characters", "200 characters",
         "300 characters", "400 characters"},
        s.deletedMessageLengthLimit,
        [](auto val) {
            return val ? QString::number(val) + " characters"
                       : QString("No limit");
        },
        [](const auto &args) {
            return fuzzyToInt(args.value, 0);
        },
        true,
        {"Limits the amount of characters displayed in deleted messages "
         "when announced via system message."});

    layout.addSeparator();

    SettingWidget::checkbox("Draw a line below the most recent message before "
                            "switching applications.",
                            s.showLastMessageIndicator)
        ->setTooltip("Adds an underline below the most recent message "
                     "sent before you tabbed out of Chatterino.")
        ->addTo(layout);

    SettingWidget::dropdown("Line style", s.lastMessagePattern)->addTo(layout);

    SettingWidget::colorButton("Line color", s.lastMessageColor)->addTo(layout);

    layout.addTitle("Visible badges");
    SettingWidget::checkbox("Authority", s.showBadgesGlobalAuthority)
        ->setTooltip("e.g. staff, admin")
        ->addTo(layout);

    SettingWidget::checkbox("Predictions", s.showBadgesPredictions)
        ->addTo(layout);

    SettingWidget::checkbox("Channel", s.showBadgesChannelAuthority)
        ->setTooltip("e.g. broadcaster, moderator")
        ->addTo(layout);

    SettingWidget::checkbox("Subscriber ", s.showBadgesSubscription)
        ->addTo(layout);

    SettingWidget::checkbox("Vanity", s.showBadgesVanity)
        ->setTooltip("e.g. prime, bits, sub gifter")
        ->addTo(layout);

    SettingWidget::checkbox("Chatterino", s.showBadgesChatterino)
        ->setTooltip("e.g. Chatterino Supporter/Contributor/Developer")
        ->addTo(layout);

    SettingWidget::checkbox("FrankerFaceZ", s.showBadgesFfz)
        ->addKeywords({"ffz"})
        ->setTooltip("e.g. Bot, FrankerFaceZ supporter, FrankerFaceZ developer")
        ->addTo(layout);
    SettingWidget::checkbox("7TV", s.showBadgesSevenTV)
        ->addKeywords({"seventv"})
        ->setTooltip("Badges for 7TV admins, developers, and supporters")
        ->addTo(layout);
    SettingWidget::checkbox("BetterTTV", s.showBadgesBttv)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    layout.addSeparator();
    SettingWidget::checkbox("Use custom FrankerFaceZ moderator badges",
                            s.useCustomFfzModeratorBadges)
        ->addKeywords({"ffz"})
        ->addTo(layout);
    SettingWidget::checkbox("Use custom FrankerFaceZ VIP badges",
                            s.useCustomFfzVipBadges)
        ->addKeywords({"ffz"})
        ->addTo(layout);

    SettingWidget::checkbox("Animate 7TV badges", s.animateSevenTVBadges)
        ->addTo(layout);

    layout.addTitle("Overlay");
    layout.addDropdown<float>(
        "Zoom factor", ZOOM_LEVELS, s.overlayScaleFactor,
        [](auto val) {
            if (val == 1)
            {
                return u"Default"_s;
            }
            return QString::number(val) + 'x';
        },
        [](const auto &args) {
            return fuzzyToFloat(args.value, 1.F);
        },
        true,
        "The final scale of the messages in the overlay is computed by "
        "multiplying this zoom factor with the global zoom level.");

    SettingWidget::intInput("Background opacity (0-255)",
                            s.overlayBackgroundOpacity,
                            {
                                .min = 0,
                                .max = 255,
                                .singleStep = 1,
                            })
        ->setTooltip(
            "Controls the opacity of the (possibly alternating) background "
            "behind messages. The color is set through the current theme. 255 "
            "corresponds to a fully opaque background.")
        ->addTo(layout);

    SettingWidget::checkbox("Enable Shadow", s.enableOverlayShadow)
        ->setTooltip("Enables a drop shadow on the overlay. This will use more "
                     "processing power.")
        ->addTo(layout);

    SettingWidget::intInput("Shadow opacity (0-255)", s.overlayShadowOpacity,
                            {
                                .min = 0,
                                .max = 255,
                                .singleStep = 1,
                            })
        ->setTooltip("Controls the opacity of the added drop shadow. 255 "
                     "corresponds to a fully opaque shadow.")
        ->addTo(layout);

    SettingWidget::colorButton("Shadow color", s.overlayShadowColor)
        ->addTo(layout);

    SettingWidget::intInput("Shadow radius", s.overlayShadowRadius,
                            {
                                .min = 0,
                                .max = 40,
                                .singleStep = 1,
                                .suffix = "dp",
                            })
        ->setTooltip("Controls how far the shadow is spread (the blur "
                     "radius) in device-independent pixels.")
        ->addTo(layout);

    SettingWidget::intInput("Shadow offset x", s.overlayShadowOffsetX,
                            {
                                .min = -20,
                                .max = 20,
                                .singleStep = 1,
                                .suffix = "dp",
                            })
        ->setTooltip("Controls how far the shadow is offset on the x axis in "
                     "device-independent pixels. A negative value offsets to "
                     "the left and a positive to the right.")
        ->addTo(layout);

    SettingWidget::intInput("Shadow offset y", s.overlayShadowOffsetY,
                            {
                                .min = -20,
                                .max = 20,
                                .singleStep = 1,
                                .suffix = "dp",
                            })
        ->setTooltip("Controls how far the shadow is offset on the y axis in "
                     "device-independent pixels. A negative value offsets to "
                     "the top and a positive to the bottom.")
        ->addTo(layout);

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
