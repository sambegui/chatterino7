#include "widgets/settingspages/GeneralPage.hpp"

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

GeneralPage::GeneralPage()
{
    this->buildLayout();
}

void GeneralPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Interface");

    {
        auto *themes = getApp()->getThemes();
        auto available = themes->availableThemes();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        available.emplace_back("System", "System");
#endif

        SettingWidget::dropdown("Theme", themes->themeName, available)
            ->addTo(layout);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        SettingWidget::dropdown("Dark system theme",
                                themes->darkSystemThemeName,
                                themes->availableThemes())
            ->setTooltip("This theme is selected if your system is in a dark "
                         "theme and you enabled the adaptive 'System' theme.")
            ->conditionallyEnabledBy(themes->themeName, "System")
            ->addTo(layout);

        SettingWidget::dropdown("Light system theme",
                                themes->lightSystemThemeName,
                                themes->availableThemes())
            ->setTooltip("This theme is selected if your system is in a light "
                         "theme and you enabled the adaptive 'System' theme.")
            ->conditionallyEnabledBy(themes->themeName, "System")
            ->addTo(layout);
#endif
    }

    layout.addDropdown<float>(
        "Zoom", ZOOM_LEVELS, s.uiScale,
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
    ComboBox *tabDirectionDropdown =
        layout.addDropdown<std::underlying_type_t<NotebookTabLocation>>(
            "Tab layout", {"Top", "Left", "Right", "Bottom"}, s.tabDirection,
            [](auto val) {
                switch (val)
                {
                    case NotebookTabLocation::Top:
                        return "Top";
                    case NotebookTabLocation::Left:
                        return "Left";
                    case NotebookTabLocation::Right:
                        return "Right";
                    case NotebookTabLocation::Bottom:
                        return "Bottom";
                }

                return "";
            },
            [](auto args) {
                if (args.value == "Bottom")
                {
                    return NotebookTabLocation::Bottom;
                }
                else if (args.value == "Left")
                {
                    return NotebookTabLocation::Left;
                }
                else if (args.value == "Right")
                {
                    return NotebookTabLocation::Right;
                }
                else
                {
                    // default to top
                    return NotebookTabLocation::Top;
                }
            },
            false);
    tabDirectionDropdown->setMinimumWidth(
        tabDirectionDropdown->minimumSizeHint().width());

    layout.addDropdown<std::underlying_type_t<NotebookTabVisibility>>(
        "Tab visibility", {"All tabs", "Only live tabs"}, s.tabVisibility,
        [](auto val) {
            switch (val)
            {
                case NotebookTabVisibility::LiveOnly:
                    return "Only live tabs";
                case NotebookTabVisibility::AllTabs:
                default:
                    return "All tabs";
            }
        },
        [](auto args) {
            if (args.value == "Only live tabs")
            {
                return NotebookTabVisibility::LiveOnly;
            }
            else
            {
                return NotebookTabVisibility::AllTabs;
            }
        },
        false, "Choose which tabs are visible in the notebook");

    SettingWidget::dropdown("Tab style", s.tabStyle)->addTo(layout);

    layout.addWidget(new FontSettingWidget(s.chatFontFamily, s.chatFontSize,
                                           s.chatFontWeight),
                     {"font", "weight", "size"});

    SettingWidget::inverseCheckbox("Show message reply context",
                                   s.hideReplyContext)
        ->setTooltip(
            "This setting will only affect how messages are shown. You can "
            "reply to a message regardless of this setting.")
        ->addTo(layout);

    SettingWidget::checkbox("Show message reply button", s.showReplyButton)
        ->setTooltip("Show a reply button next to every chat message")
        ->addTo(layout);

    auto removeTabSeq = getApp()->getHotkeys()->getDisplaySequence(
        HotkeyCategory::Window, "removeTab");
    QString removeTabShortcut = "an assigned hotkey (Window -> remove tab)";
    if (!removeTabSeq.isEmpty())
    {
        removeTabShortcut =
            removeTabSeq.toString(QKeySequence::SequenceFormat::NativeText);
    }

    SettingWidget::checkbox("Show tab close button", s.showTabCloseButton)
        ->setTooltip(
            "When disabled, the x to close a tab will be hidden.\nTabs can "
            "still be closed by right-clicking or pressing " +
            removeTabShortcut + ".")
        ->addTo(layout);

    SettingWidget::checkbox("Always on top", s.windowTopMost)
        ->setTooltip("Always keep Chatterino as the top window.")
        ->addTo(layout);

#ifdef USEWINSDK
    SettingWidget::checkbox("Start with Windows", s.autorun)
        ->setTooltip("Start Chatterino when your computer starts.")
        ->addTo(layout);
#endif
    if (!BaseWindow::supportsCustomWindowFrame())
    {
        auto settingsSeq = getApp()->getHotkeys()->getDisplaySequence(
            HotkeyCategory::Window, "openSettings");
        QString shortcut = " (no key bound to open them otherwise)";
        // TODO: maybe prevent the user from locking themselves out of the settings?
        if (!settingsSeq.isEmpty())
        {
            shortcut = QStringLiteral(" (%1 to show)")
                           .arg(settingsSeq.toString(
                               QKeySequence::SequenceFormat::NativeText));
        }

        SettingWidget::inverseCheckbox("Show preferences button" + shortcut,
                                       s.hidePreferencesButton)
            ->addTo(layout);

        SettingWidget::inverseCheckbox("Show user button", s.hideUserButton)
            ->addTo(layout);
    }

    SettingWidget::checkbox("Mark tabs with live channels", s.showTabLive)
        ->setTooltip("Shows a red dot in the top right corner of a tab to "
                     "indicate one of the channels in the tab is live.")
        ->addTo(layout);

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
