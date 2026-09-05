#include "widgets/settingspages/EmotesPage.hpp"

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

EmotesPage::EmotesPage()
{
    this->buildLayout();
}

void EmotesPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Emotes");

    SettingWidget::checkbox("Enable", s.enableEmoteImages)->addTo(layout);

    SettingWidget::checkbox("Animate", s.animateEmotes)->addTo(layout);

    SettingWidget::checkbox("Animate only when Chatterino is focused",
                            s.animationsWhenFocused)
        ->addTo(layout);

    SettingWidget::checkbox("Enable zero-width emotes", s.enableZeroWidthEmotes)
        ->setTooltip(
            "When disabled, emotes that overlap other emotes, such as BTTV's "
            "cvMask and 7TV's RainTime, will appear as normal emotes.")
        ->addTo(layout);

    SettingWidget::checkbox("Enable emote completion by typing :",
                            s.emoteCompletionWithColon)
        ->setTooltip(
            "With this setting enabled, typing the colon character opens the "
            "colon-completion popup which gives you an updating list of emotes "
            "matching the text after the colon.")
        ->addTo(layout);

    SettingWidget::checkbox("Use experimental smarter emote completion.",
                            s.useSmartEmoteCompletion)
        ->addTo(layout);

    layout.addDropdown<float>(
        "Size", {"0.5x", "0.75x", "Default", "1.25x", "1.5x", "2x"},
        s.emoteScale,
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

    SettingWidget::checkbox("Remove spaces between emotes",
                            s.removeSpacesBetweenEmotes)
        ->setTooltip("When enabled, adjacent emotes will no longer have an "
                     "added space separating them.")
        ->addTo(layout);

    SettingWidget::checkbox("Show unlisted 7TV emotes",
                            s.showUnlistedSevenTVEmotes)
        ->addKeywords({"seventv"})
        ->addTo(layout);
    // TODO: Add a tooltip explaining what an unlisted 7TV emote is
    // but wait until https://github.com/Chatterino/wiki/pull/255 is resolved,
    // as an official description from 7TV devs is best
    s.showUnlistedSevenTVEmotes.connect(
        []() {
            getApp()->getTwitch()->forEachChannelAndSpecialChannels(
                [](const auto &c) {
                    if (c->isTwitchChannel())
                    {
                        auto *channel = dynamic_cast<TwitchChannel *>(c.get());
                        if (channel != nullptr)
                        {
                            channel->refreshSevenTVChannelEmotes(false);
                        }
                    }
                });
        },
        false);

    SettingWidget::dropdown("Show emote & badge thumbnail on hover",
                            s.emotesTooltipPreview)
        ->addTo(layout);

    SettingWidget::dropdown("Emote & badge thumbnail size on hover",
                            s.emoteTooltipScale)
        ->addTo(layout);

    SettingWidget::dropdown("Emoji style", s.emojiSet)->addTo(layout);

    SettingWidget::checkbox("Show BetterTTV global emotes",
                            s.enableBTTVGlobalEmotes)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox("Show BetterTTV channel emotes",
                            s.enableBTTVChannelEmotes)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox(
        "Enable BetterTTV live emote updates (requires restart)",
        s.enableBTTVLiveUpdates)
        ->addKeywords({"bttv"})
        ->addTo(layout);
    SettingWidget::checkbox("Send activity to BetterTTV", s.sendBTTVActivity)
        ->setTooltip(
            "When enabled, Chatterino will signal an activity to BetterTTV "
            "when you send a chat message. This is used for badges, "
            " and personal emotes. When disabled, no activity "
            "is sent and others won't see your cosmetics.")
        ->addKeywords({"bttv"})
        ->addTo(layout);

    SettingWidget::checkbox("Show FrankerFaceZ global emotes",
                            s.enableFFZGlobalEmotes)
        ->addKeywords({"ffz"})
        ->addTo(layout);
    SettingWidget::checkbox("Show FrankerFaceZ channel emotes",
                            s.enableFFZChannelEmotes)
        ->addKeywords({"ffz"})
        ->addTo(layout);

    SettingWidget::checkbox("Show 7TV global emotes",
                            s.enableSevenTVGlobalEmotes)
        ->addKeywords({"seventv"})
        ->addTo(layout);
    SettingWidget::checkbox("Show 7TV channel emotes",
                            s.enableSevenTVChannelEmotes)
        ->addKeywords({"seventv"})
        ->addTo(layout);
    SettingWidget::checkbox("Show 7TV channel emotes",
                            s.enableSevenTVPersonalEmotes)
        ->addKeywords({"seventv"})
        ->setTooltip("This requires '7TV live updates' to work.")
        ->addTo(layout);
    SettingWidget::checkbox("Enable 7TV live emote updates (requires restart)",
                            s.enableSevenTVEventAPI)
        ->addKeywords({"seventv"})
        ->setTooltip("When enabled, channel emotes will get updated "
                     "automatically (no reload required) and cosmetics "
                     "(badges/paints/personal emotes) will get updated.")
        ->addTo(layout);
    SettingWidget::checkbox("Send activity to 7TV", s.sendSevenTVActivity)
        ->setTooltip("When enabled, Chatterino will signal an activity to 7TV "
                     "when you send a chat message. This is used for badges, "
                     "paints, and personal emotes. When disabled, no activity "
                     "is sent and others won't see your cosmetics.")
        ->addKeywords({"seventv"})
        ->addTo(layout);

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
