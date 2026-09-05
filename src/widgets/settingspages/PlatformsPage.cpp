#include "widgets/settingspages/PlatformsPage.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"  // IWYU pragma: keep
#include "common/Version.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyCategory.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/youtube/YouTubeAccount.hpp"
#include "providers/youtube/YouTubeApi.hpp"
#include "providers/youtube/YouTubeChatServer.hpp"
#include "providers/youtube/YouTubeOAuthFlow.hpp"
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

PlatformsPage::PlatformsPage()
{
    this->buildLayout();
}

void PlatformsPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Twitch private API");
    layout.addDescription(
        "Twitch has no public API for pinning a message, running a prediction "
        "or redeeming channel points, so these go through the same private "
        "endpoint their website uses. That means sending Twitch's own client "
        "id with your token, which their Developer Agreement does not allow "
        "and which accounts have occasionally been actioned for. It is off "
        "unless you turn it on. Paste the value of the auth-token cookie "
        "from twitch.tv while logged in. Never run a script someone hands you "
        "to fetch it.");

    SettingWidget::checkbox("Enable Twitch's private API", s.enableTwitchGql)
        ->setTooltip("Turns on /pin and /unpin. Read the warning above first.")
        ->addTo(layout);
    {
        auto *gqlForm = new QFormLayout();
        layout.addLayout(gqlForm);

        SettingWidget::lineEdit("Twitch token", s.twitchGqlToken,
                                "auth-token cookie value")
            ->addTo(layout, gqlForm);
    }

    layout.addTitle("Kick Integration");
    layout.addDescription(
        "Enable Kick.com chat integration to view and send messages to Kick "
        "channels. You can also merge Twitch and Kick channels into a single "
        "view.");

    SettingWidget::checkbox("Enable Kick integration", s.enableKickIntegration)
        ->setTooltip(
            "When enabled, you can add Kick.com channels and optionally merge "
            "them with Twitch channels for a unified chat experience. "
            "Requires restart to fully take effect.")
        ->addTo(layout);

    // Kick account login status and button
    {
        auto *kickAccountLayout = new QHBoxLayout();

        auto *kickStatusLabel = new QLabel("Not logged in to Kick");
        kickStatusLabel->setStyleSheet("color: #888;");
        kickAccountLayout->addWidget(kickStatusLabel);

        auto *kickLoginButton = new QPushButton("Login with Kick");
        kickLoginButton->setEnabled(s.enableKickIntegration);
        kickLoginButton->setToolTip(
            "Note: Kick OAuth requires a registered developer application.\n"
            "Set CHATTERINO_KICK_CLIENT_ID environment variable with your\n"
            "app's client ID. Without this, login will fail.");
        kickAccountLayout->addWidget(kickLoginButton);

        auto *kickLogoutButton = new QPushButton("Logout");
        kickLogoutButton->setEnabled(false);
        kickLogoutButton->setVisible(false);
        kickAccountLayout->addWidget(kickLogoutButton);

        kickAccountLayout->addStretch();
        layout.addLayout(kickAccountLayout);

        // Update UI based on Kick integration state
        auto updateKickUI = [kickLoginButton, kickLogoutButton,
                             kickStatusLabel] {
            auto *app = getApp();
            bool enabled = getSettings()->enableKickIntegration;
            bool loggedIn = app->getAccounts()->kick.isLoggedIn();

            kickLoginButton->setEnabled(enabled && !loggedIn);
            kickLoginButton->setVisible(!loggedIn);
            kickLogoutButton->setEnabled(enabled && loggedIn);
            kickLogoutButton->setVisible(loggedIn);

            if (!enabled)
            {
                kickStatusLabel->setText("Kick integration disabled");
                kickStatusLabel->setStyleSheet("color: #888;");
            }
            else if (loggedIn)
            {
                QString username =
                    app->getAccounts()->kick.getCurrentUsername();
                kickStatusLabel->setText(
                    QString("Logged in as: %1").arg(username));
                kickStatusLabel->setStyleSheet("color: #53fc18;");
            }
            else
            {
                kickStatusLabel->setText("Not logged in to Kick");
                kickStatusLabel->setStyleSheet("color: #888;");
            }
        };

        // Initial state
        updateKickUI();

        // Connect settings change
        s.enableKickIntegration.connect([updateKickUI](const auto &, auto) {
            updateKickUI();
        });

        // Connect account changes
        getApp()->getAccounts()->kick.currentUserChanged.connect(updateKickUI);

        // Login button
        QObject::connect(kickLoginButton, &QPushButton::clicked, [] {
            getApp()->getAccounts()->kick.startLogin();
        });

        // Logout button
        QObject::connect(kickLogoutButton, &QPushButton::clicked,
                         [updateKickUI] {
                             getApp()->getAccounts()->kick.logout();
                             updateKickUI();
                         });

        // Login result signals
        std::ignore = getApp()->getAccounts()->kick.loginSucceeded.connect(
            [updateKickUI](const auto &) {
                updateKickUI();
            });

        std::ignore = getApp()->getAccounts()->kick.loginFailed.connect(
            [kickStatusLabel](const QString &error) {
                kickStatusLabel->setText(
                    QString("Login failed: %1").arg(error));
                kickStatusLabel->setStyleSheet("color: #ff4444;");
            });
    }

    layout.addTitle("YouTube Integration");
    layout.addDescription(
        "Read YouTube live chat and merge it with Twitch and Kick in one "
        "split. Reading needs no account at all. Signing in is only required "
        "to <em>send</em> messages.");

    SettingWidget::checkbox("Enable YouTube integration",
                            s.enableYouTubeIntegration)
        ->setTooltip(
            "When enabled you can add YouTube live chats by @handle, channel "
            "URL, or video ID, and merge them with Twitch and Kick channels.")
        ->addTo(layout);

    {
        auto *youtubeAccountLayout = new QHBoxLayout();

        auto *youtubeStatusLabel = new QLabel();
        youtubeAccountLayout->addWidget(youtubeStatusLabel);

        auto *youtubeLoginButton = new QPushButton("Sign in with Google");
        youtubeLoginButton->setToolTip(
            "Sending uses the official YouTube Data API, which needs your own "
            "OAuth client.\n"
            "Create a Desktop app client in the Google Cloud console, then "
            "set\n"
            "CHATTERINO_YOUTUBE_CLIENT_ID and CHATTERINO_YOUTUBE_CLIENT_SECRET.");
        youtubeAccountLayout->addWidget(youtubeLoginButton);

        auto *youtubeLogoutButton = new QPushButton("Sign out");
        youtubeAccountLayout->addWidget(youtubeLogoutButton);

        youtubeAccountLayout->addStretch();
        layout.addLayout(youtubeAccountLayout);

        auto updateYouTubeUI = [youtubeStatusLabel, youtubeLoginButton,
                                youtubeLogoutButton] {
            auto *server = getApp()->getYouTubeChatServer();
            bool signedIn = server->isSignedIn();
            bool configured = YouTubeApi::hasClientCredentials();

            if (signedIn)
            {
                auto name = server->account()->getDisplayName();
                youtubeStatusLabel->setText(
                    name.isEmpty()
                        ? QStringLiteral("Signed in to YouTube")
                        : QStringLiteral("Signed in as %1").arg(name));
                youtubeStatusLabel->setStyleSheet("color: #2ba644;");
            }
            else if (!configured)
            {
                youtubeStatusLabel->setText(
                    "Sending disabled: CHATTERINO_YOUTUBE_CLIENT_ID and "
                    "_SECRET are not set");
                youtubeStatusLabel->setStyleSheet("color: #888;");
            }
            else
            {
                youtubeStatusLabel->setText("Not signed in (reading still "
                                            "works)");
                youtubeStatusLabel->setStyleSheet("color: #888;");
            }

            youtubeLoginButton->setEnabled(configured && !signedIn);
            youtubeLogoutButton->setEnabled(signedIn);
            youtubeLogoutButton->setVisible(signedIn);
        };

        updateYouTubeUI();

        QObject::connect(
            youtubeLoginButton, &QPushButton::clicked,
            [youtubeStatusLabel, updateYouTubeUI] {
                auto *flow = new YouTubeOAuthFlow(getApp()->getYouTubeChatServer()
                                                      ->api()
                                                      .get());

                QObject::connect(
                    flow, &YouTubeOAuthFlow::codeReceived,
                    [flow, updateYouTubeUI, youtubeStatusLabel](
                        const QString &code, const QString &redirectUri) {
                        getApp()->getYouTubeChatServer()->api()->exchangeCode(
                            code, redirectUri,
                            [flow, updateYouTubeUI,
                             youtubeStatusLabel](bool ok, QString error) {
                                if (!ok)
                                {
                                    youtubeStatusLabel->setText(
                                        QStringLiteral("Login failed: %1")
                                            .arg(error));
                                    youtubeStatusLabel->setStyleSheet(
                                        "color: #ff4444;");
                                }
                                else
                                {
                                    updateYouTubeUI();
                                }
                                flow->deleteLater();
                            });
                    });

                QObject::connect(flow, &YouTubeOAuthFlow::failed,
                                 [flow, youtubeStatusLabel](
                                     const QString &error) {
                                     youtubeStatusLabel->setText(
                                         QStringLiteral("Login failed: %1")
                                             .arg(error));
                                     youtubeStatusLabel->setStyleSheet(
                                         "color: #ff4444;");
                                     flow->deleteLater();
                                 });

                youtubeStatusLabel->setText(
                    "Waiting for Google in your browser...");
                youtubeStatusLabel->setStyleSheet("color: #888;");
                flow->start();
            });

        QObject::connect(youtubeLogoutButton, &QPushButton::clicked,
                         [updateYouTubeUI] {
                             getApp()->getYouTubeChatServer()->signOut();
                             updateYouTubeUI();
                         });
    }

    layout.addStretch();

    // invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
