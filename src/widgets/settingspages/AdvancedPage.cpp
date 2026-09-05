#include "widgets/settingspages/AdvancedPage.hpp"

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

const QString CHROME_EXTENSION_LINK =
    u"https://chrome.google.com/webstore/detail/chatterino-native-host/glknmaideaikkmemifbfkhnomoknepka"_s;
const QString FIREFOX_EXTENSION_LINK =
    u"https://addons.mozilla.org/en-US/firefox/addon/chatterino-native-host/"_s;

}  // namespace

namespace chatterino {

AdvancedPage::AdvancedPage()
{
    this->buildLayout();
    this->initExtra();
}

void AdvancedPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Advanced");

    if (supportsIncognitoLinks())
    {
        SettingWidget::checkbox("Open links in incognito/private mode",
                                s.openLinksIncognito)
            ->addTo(layout);
    }

    SettingWidget::customCheckbox(
        "Restart on crash (requires restart)",
        getApp()->getCrashHandler()->shouldRecover(),
        [](bool on) {
            getApp()->getCrashHandler()->saveShouldRecover(on);
        })
        ->setTooltip("When possible, restart Chatterino if the program crashes")
        ->addTo(layout);

#if defined(Q_OS_LINUX) && !defined(NO_QTKEYCHAIN)
    if (!getApp()->getPaths().isPortable())
    {
        SettingWidget::checkbox(
            "Use libsecret/KWallet/Gnome keychain to secure passwords",
            s.useKeyring)
            ->addTo(layout);
    }
#endif

    SettingWidget::checkbox("Show 7TV Animated Profile Picture",
                            s.displaySevenTVAnimatedProfile)
        ->addTo(layout);
    SettingWidget::checkbox("Load AVIF images", s.allowAvifImages)
        ->setTooltip(
            "When enabled and an AVIF decoder is found, AVIF images will be "
            "preferred over WEBP on 7TV. This saves bandwidth.")
        ->addTo(layout);

    SettingWidget::inverseCheckbox("Show moderation messages",
                                   s.hideModerationActions)
        ->setTooltip(
            "Show messages for timeouts, bans, and other moderator actions.")
        ->addTo(layout);

    SettingWidget::inverseCheckbox("Show deletions of single messages",
                                   s.hideDeletionActions)
        ->setTooltip("Show when a single message is deleted.\ne.g. A message "
                     "from TreuKS was deleted: abc")
        ->addTo(layout);

    SettingWidget::checkbox("Colorize users without color set (gray names)",
                            s.colorizeNicknames)
        ->setTooltip("Grant a random color to users who never set a color for "
                     "themselves")
        ->addTo(layout);

    SettingWidget::checkbox("Mention users with a comma",
                            s.mentionUsersWithComma)
        ->setTooltip("When using tab-completon, if the username is at the "
                     "start of the message, include a comma at the end of the "
                     "name.\ne.g. pajl -> pajlada,")
        ->addTo(layout);

    SettingWidget::checkbox("Show joined users (< 1000 chatters)", s.showJoins)
        ->setTooltip(
            "Show a Twitch system message stating what users have joined the "
            "chat, only available when the chat has less than 1000 users")
        ->addTo(layout);

    SettingWidget::checkbox("Show parted users (< 1000 chatters)", s.showParts)
        ->setTooltip(
            "Show a Twitch system message stating what users have left the "
            "chat, only available when chat has less than 1000 users")
        ->addTo(layout);

    SettingWidget::checkbox(
        "Automatically close user popup when it loses focus",
        s.autoCloseUserPopup)
        ->addTo(layout);

    SettingWidget::checkbox(
        "Automatically close reply thread popup when it loses focus",
        s.autoCloseThreadPopup)
        ->addTo(layout);

    SettingWidget::checkbox("Display 7TV Paints", s.displaySevenTVPaints)
        ->addTo(layout);
    SettingWidget::checkbox("Display 7TV Paint Shadows",
                            s.displaySevenTVPaintShadows)
        ->addTo(layout);

    SettingWidget::checkbox("Lowercase domains (anti-phishing)",
                            s.lowercaseDomains)
        ->setTooltip(
            "Make all clickable links lowercase to deter phishing attempts.")
        ->addTo(layout);

    SettingWidget::checkbox("Show user's pronouns in user card", s.showPronouns)
        ->setDescription(
            R"(Pronouns are retrieved from <a href="https://pr.alejo.io">pr.alejo.io</a> when a user card is opened.)")
        ->addTo(layout);

    SettingWidget::checkbox("Show stream title in live message",
                            s.showTitleInLiveMessage)
        ->setTooltip("The title in the message will be the title the streamer "
                     "set when they went live, and will not update as the "
                     "streamer updates their title.")
        ->addTo(layout);

    SettingWidget::checkbox("Bold @usernames", s.boldUsernames)
        ->setTooltip("Bold @mentions to make them more noticeable.")
        ->addTo(layout);

    SettingWidget::checkbox("Color @usernames", s.colorUsernames)
        ->setTooltip("If Chatterino has seen a user, highlight @mention's of "
                     "them with their Twitch color.")
        ->addTo(layout);

    SettingWidget::checkbox("Try to find usernames without @ prefix",
                            s.findAllUsernames)
        ->setTooltip("Find mentions of users in chat without the @ prefix.")
        ->addTo(layout);

    SettingWidget::checkbox("Show username autocompletion popup menu",
                            s.showUsernameCompletionMenu)
        ->addTo(layout);

    SettingWidget::checkbox("Always include broadcaster in user completions",
                            s.alwaysIncludeBroadcasterInUserCompletions)
        ->setTooltip(
            "This will ensure a broadcaster is always easy to ping, even if "
            "they don't have chat open or have typed recently.")
        ->addTo(layout);

    const QStringList usernameDisplayModes = {"Username", "Localized name",
                                              "Username and localized name"};

    ComboBox *nameDropdown =
        layout.addDropdown<std::underlying_type_t<UsernameDisplayMode>>(
            "Username style", usernameDisplayModes, s.usernameDisplayMode,
            [usernameDisplayModes](auto val) {
                return usernameDisplayModes.at(val - 1);
                // UsernameDisplayMode enum indexes from 1
            },
            [](auto args) {
                return args.index + 1;
            },
            false,
            "Customizes how you see Asian Language names.\nUsing an option "
            "that includes \"localized\" will display the username in it's "
            "respective Asian language.\ne.g. "
            "Username and localized: testaccount_420(테스트계정420)\n"
            "Username: testaccount_420\n"
            "Localized name: 테스트계정420");
    nameDropdown->setMinimumWidth(nameDropdown->minimumSizeHint().width());

    layout.addDropdown<float>(
        "Username font weight", {"50", "Default", "75", "100"}, s.boldScale,
        [](auto val) {
            if (val == 63)
            {
                return QString("Default");
            }
            else
            {
                return QString::number(val);
            }
        },
        [](auto args) {
            return fuzzyToFloat(args.value, 63.f);
        });

    SettingWidget::checkbox(
        "Double click to open links and other elements in chat",
        s.linksDoubleClickOnly)
        ->setTooltip("When enabled, opening links/usercards requires "
                     "double-clicking.\nUseful making sure you don't "
                     "accidentally click on suspicious links.")
        ->addTo(layout);

    SettingWidget::checkbox("Unshorten links", s.unshortLinks)
        ->setTooltip("When enabled, \"right-click + copy link\" will copy the "
                     "unshortened version of the link.\ne.g. "
                     "https://bit.ly/mrfors -> https://forsen.tv/")
        ->addTo(layout);

    SettingWidget::checkbox(
        "Only search for emote autocompletion at the start of emote names",
        s.prefixOnlyEmoteCompletion)
        ->setTooltip("When disabled, emote tab-completion will complete based "
                     "on any part of the name.\ne.g. sheffy -> DatSheffy")
        ->addTo(layout);

    SettingWidget::checkbox("Only search for username autocompletion with an @",
                            s.userCompletionOnlyWithAt)
        ->setTooltip("When enabled, username tab-completion will only complete "
                     "when using @\ne.g. pajl -> pajl | @pajl -> @pajlada")
        ->addTo(layout);

    SettingWidget::checkbox("Show Twitch whispers inline", s.inlineWhispers)
        ->setTooltip("Show whispers as messages in all splits instead of just "
                     "/whispers.")
        ->addTo(layout);

    SettingWidget::checkbox("Highlight received inline whispers",
                            s.highlightInlineWhispers)
        ->setTooltip(
            "Highlight the whispers shown in all splits.\nIf \"Show Twitch "
            "whispers inline\" is disabled, this setting will do nothing.")
        ->addTo(layout);

    SettingWidget::checkbox(
        "Automatically subscribe to participated reply threads",
        s.autoSubToParticipatedThreads)
        ->setTooltip(
            "When enabled, you will automatically subscribe to reply threads "
            "you participate in.\nThis means reply threads you participate in "
            "will use your \"Subscribed Reply Threads\" highlight settings.")
        ->addTo(layout);

    SettingWidget::checkbox("Load message history on connect",
                            s.loadTwitchMessageHistoryOnConnect)
        ->addTo(layout);

    // TODO: Change phrasing to use better english once we can tag settings, right now it's kept as history instead of historical so that the setting shows up when the user searches for history
    SettingWidget::intInput("Max number of history messages to load on connect",
                            s.twitchMessageHistoryLimit,
                            {
                                .min = 10,
                                .max = 800,
                                .singleStep = 10,
                            })
        ->addTo(layout);

    SettingWidget::intInput("Split message scrollback limit (requires restart)",
                            s.scrollbackSplitLimit,
                            {
                                .min = 100,
                                .max = 100000,
                                .singleStep = 100,
                            })
        ->addTo(layout);

    SettingWidget::intInput("Usercard scrollback limit (requires restart)",
                            s.scrollbackUsercardLimit,
                            {
                                .min = 100,
                                .max = 100000,
                                .singleStep = 100,
                            })
        ->addTo(layout);

    SettingWidget::dropdown("Show blocked term automod messages",
                            s.showBlockedTermAutomodMessages)
        ->setTooltip("Show messages that are blocked by AutoMod for containing "
                     "a public blocked term in the current channel.")
        ->addTo(layout);

    layout.addDropdown<int>(
        "Stack timeouts", {"Stack", "Stack until timeout", "Don't stack"},
        s.timeoutStackStyle,
        [](int index) {
            return index;
        },
        [](auto args) {
            return args.index;
        },
        false, "Combine consecutive timeout messages into a single message.");

    SettingWidget::checkbox("Combine multiple bit tips into one", s.stackBits)
        ->setTooltip("Combine consecutive cheermotes (sent in a single "
                     "message) into one cheermote.")
        ->addTo(layout);

    // update this tooltip if https://github.com/Chatterino/chatterino2/pull/1557 is ever merged
    SettingWidget::checkbox("Messages in /mentions highlights tab",
                            s.highlightMentions)
        ->setTooltip("When disabled, the /mentions tab will not highlight in "
                     "red when you are mentioned.")
        ->addTo(layout);

    SettingWidget::checkbox("Strip leading mention in replies",
                            s.stripReplyMention)
        ->setTooltip(
            "When disabled, messages sent in reply threads will include the "
            "@mention for the related thread. If the reply context is hidden, "
            "these mentions will never be stripped.")
        ->addTo(layout);

    SettingWidget::dropdown("Chat send protocol", s.chatSendProtocol)
        ->setTooltip("'Helix' will use Twitch's Helix API to send message. "
                     "'IRC' will use IRC to send messages.")
        ->addTo(layout);

    SettingWidget::checkbox("Show send message button", s.showSendButton)
        ->setTooltip("Show a Send button next to each split input that can be "
                     "clicked to send the message")
        ->addTo(layout);

    SettingWidget::dropdown("Sound backend (requires restart)", s.soundBackend)
        ->setTooltip("Change this only if you're noticing issues with sound "
                     "playback on your system")
        ->addTo(layout);

    SettingWidget::checkbox(
        "Enable experimental Twitch EventSub support (requires restart)",
        s.enableExperimentalEventSub)
        ->addTo(layout);

    SettingWidget::checkbox("Disable renaming of tabs on double-click",
                            s.disableTabRenamingOnClick)
        ->setTooltip("Prevents the rename dialog from opening when a tab is "
                     "double-clicked")
        ->addTo(layout);

    layout.addNavigationSpacing();
    layout.addTitle("Beta");
    if (Version::instance().isSupportedOS())
    {
        layout.addDescription(
            "You can receive updates earlier by ticking the box below. Report "
            "issues <a href='https://chatterino.com/link/issues'>here</a>.");

        SettingWidget::checkbox("Receive beta updates", s.betaUpdates)
            ->addTo(layout);
    }
    else
    {
        layout.addDescription(
            "Your operating system is not officially supplied with builds. For "
            "updates, please rebuild Chatterino from sources. Report "
            "issues <a href='https://chatterino.com/link/issues'>here</a>.");
    }

    layout.addTitle("Browser Integration");
#ifdef Q_OS_WIN
    layout.addDescription(
        "The browser extension replaces the default "
        "Twitch.tv chat with Chatterino, and updates the /watching split on "
        "Chatterino when Twitch.tv is open.");
#else
    layout.addDescription("The browser extension updates the /watching "
                          "split on Chatterino when Twitch.tv is open.");
#endif

    {
        if (auto err = nmIpcError().get())
        {
            layout.addDescription(
                "An error happened during initialization of the "
                "browser extension: " +
                *err);
        }
    }

    layout.addDescription(formatRichNamedLink(
        CHROME_EXTENSION_LINK,
        "Download for Google Chrome and similar browsers."));
    layout.addDescription(
        formatRichNamedLink(FIREFOX_EXTENSION_LINK, "Download for Firefox"));

#ifdef Q_OS_WIN
    layout.addDescription("Chatterino only attaches to known browsers to avoid "
                          "attaching to other windows by accident.");
    SettingWidget::checkbox("Attach to any browser (may cause issues)",
                            s.attachExtensionToAnyProcess)
        ->setTooltip(
            "Attempt to force the Chatterino Browser Extension to work in "
            "certain browsers that do not work automatically.\ne.g. Librewolf")
        ->addTo(layout);
#endif

    {
        auto *note = layout.addDescription(
            "A semicolon-separated list of Chrome or Firefox extension IDs "
            "allowed to interact with Chatterino's browser integration "
            "(requires restart).\n"
            "Using multiple extension IDs from different browsers may cause "
            "issues.");
        note->setWordWrap(true);
        note->setStyleSheet("color: #bbb");

        layout.addWidget(note);

        auto *form = new QFormLayout();
        layout.addLayout(form);

        SettingWidget::lineEdit("Extra extension IDs", s.additionalExtensionIDs,
                                "Extension;IDs;separated;by;semicolons")
            ->addTo(layout, form);
    }

    layout.addTitle("AppData & Cache");

    layout.addSubtitle("Application Data");
    layout.addDescription("All local files like settings and cache files are "
                          "store in this directory.");
    layout.addButton("Open AppData directory", [] {
#ifdef Q_OS_DARWIN
        QDesktopServices::openUrl("file://" +
                                  getApp()->getPaths().rootAppDataDirectory);
#else
        QDesktopServices::openUrl(getApp()->getPaths().rootAppDataDirectory);
#endif
    });

    layout.addSubtitle("Temporary files (Cache)");
    layout.addDescription(
        "Files that are used often (such as emotes) are saved to disk to "
        "reduce bandwidth usage and to speed up loading.");

    auto *cachePathLabel = layout.addDescription("placeholder :D");
    getSettings()->cachePath.connect([cachePathLabel](const auto &,
                                                      auto) mutable {
        QString newPath = getApp()->getPaths().cacheDirectory();

        QString pathShortened = "Cache saved at <a href=\"file:///" + newPath +
                                R"("><span style="color: white;">)" +
                                shortenString(newPath, 50) + "</span></a>";
        cachePathLabel->setText(pathShortened);
        cachePathLabel->setToolTip(newPath);
    });

    // Choose and reset buttons
    {
        auto *box = new QHBoxLayout;

        box->addWidget(layout.makeButton("Choose cache path", [this]() {
            getSettings()->cachePath = QFileDialog::getExistingDirectory(this);
        }));
        box->addWidget(layout.makeButton("Reset", []() {
            getSettings()->cachePath = "";
        }));
        box->addWidget(layout.makeButton("Clear Cache", [&layout]() {
            auto reply = QMessageBox::question(
                layout.window(), "Clear cache",
                "Are you sure that you want to clear your cache? Emotes may "
                "take longer to load next time Chatterino is started.",
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes)
            {
                auto cacheDir = QDir(getApp()->getPaths().cacheDirectory());
                cacheDir.removeRecursively();
                cacheDir.mkdir(getApp()->getPaths().cacheDirectory());
            }
        }));
        box->addStretch(1);

        layout.addLayout(box);
    }

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

void AdvancedPage::initExtra()
{
    /// update cache path
    if (this->cachePath_)
    {
        getSettings()->cachePath.connect(
            [cachePath = this->cachePath_](const auto &, auto) mutable {
                QString newPath = getApp()->getPaths().cacheDirectory();

                QString pathShortened = "Current location: <a href=\"file:///" +
                                        newPath + "\">" +
                                        shortenString(newPath, 50) + "</a>";

                cachePath->setText(pathShortened);
                cachePath->setToolTip(newPath);
            });
    }
}

}  // namespace chatterino
