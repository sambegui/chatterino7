#include "widgets/settingspages/StreamerModePage.hpp"

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

StreamerModePage::StreamerModePage()
{
    this->buildLayout();
}

void StreamerModePage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Streamer Mode");
    layout.addDescription(
        "Chatterino can automatically change behavior if it detects that any "
        "streaming software is running.\nSelect which things you want to "
        "change while streaming");

    SettingWidget::dropdown("Enable Streamer Mode", s.enableStreamerMode)
        ->addTo(layout);

    SettingWidget::checkbox("Hide usercard avatars",
                            s.streamerModeHideUsercardAvatars)
        ->setTooltip("Prevent potentially explicit avatars from showing.")
        ->addTo(layout);

    SettingWidget::checkbox("Hide link thumbnails",
                            s.streamerModeHideLinkThumbnails)
        ->setTooltip("Prevent potentially explicit thumbnails from showing "
                     "when hovering links.")
        ->addTo(layout);

    SettingWidget::checkbox(
        "Hide viewer count and stream length while hovering over split header",
        s.streamerModeHideViewerCountAndDuration)
        ->addTo(layout);

    SettingWidget::checkbox("Hide moderation actions",
                            s.streamerModeHideModActions)
        ->setTooltip(
            "Hide bans, timeouts, and automod messages from appearing in chat.")
        ->addTo(layout);

    SettingWidget::checkbox("Hide messages from restricted users",
                            s.streamerModeHideRestrictedUsers)
        ->setTooltip("Restricted users can be marked by you, your moderators, "
                     "or Twitch's AutoMod")
        ->addTo(layout);

    SettingWidget::checkbox("Hide blocked terms",
                            s.streamerModeHideBlockedTermText)
        ->setTooltip(
            "Hide blocked terms from showing up in places like AutoMod "
            "messages. This can be useful in case you have some blocked terms "
            "that you don't want to show on stream.")
        ->addTo(layout);

    SettingWidget::checkbox("Mute mention sounds", s.streamerModeMuteMentions)
        ->setTooltip("Mute your ping sound from playing.")
        ->addTo(layout);

    SettingWidget::checkbox("Suppress Live Notifications",
                            s.streamerModeSuppressLiveNotifications)
        ->setTooltip(
            "Hide Live notification popups from appearing. (Windows Only)")
        ->addTo(layout);

    SettingWidget::checkbox("Suppress Inline Whispers",
                            s.streamerModeSuppressInlineWhispers)
        ->setTooltip("Hide whispers sent to you from appearing in chat.")
        ->addTo(layout);

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
